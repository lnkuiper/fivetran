#include "optimizers.hpp"

#include "functions.hpp"
#include "duckdb/function/scalar/struct_functions.hpp"
#include "duckdb/function/scalar/variant_functions.hpp"
#include "duckdb/optimizer/optimizer.hpp"
#include "duckdb/optimizer/column_binding_replacer.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/operator/logical_projection.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/planner/expression/bound_cast_expression.hpp"

namespace duckdb {

class SparseBuildOptimizer : public OptimizerExtension {
public:
	SparseBuildOptimizer() {
		optimize_function = Optimize;
	}

private:
#ifdef DEBUG
	static constexpr idx_t BUILD_COLUMN_THRESHOLD = 0;
#else
	static constexpr idx_t BUILD_COLUMN_THRESHOLD = 10;
#endif

private:
	static void OptimizeInternal(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan,
	                             LogicalOperator &root) {
		for (auto &child : plan->children) {
			OptimizeInternal(input, child, root);
		}

		if (!IsEligible(*plan)) {
			return;
		}

		const auto bindings_before = plan->GetColumnBindings();
		SparsifyBuild(input, plan);
		ReplaceBindings(plan, root, bindings_before);
	}

	static bool IsEligible(const LogicalOperator &op) {
		if (op.type != LogicalOperatorType::LOGICAL_COMPARISON_JOIN) {
			return false;
		}
		if (op.children[1]->types.size() < BUILD_COLUMN_THRESHOLD) {
			return false;
		}

		const auto &comparison_join = op.Cast<LogicalComparisonJoin>();
		switch (comparison_join.join_type) {
		case JoinType::LEFT:
			return true;
		default:
			break;
		}

		for (const auto &condition : comparison_join.conditions) {
			if (condition.right->GetExpressionClass() != ExpressionClass::BOUND_COLUMN_REF) {
				return false;
			}
		}

		// TODO need to do something about RHS projection map!

		return true;
	}

	static void SparsifyBuild(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
		D_ASSERT(IsEligible(*plan));
		auto &comparison_join = plan->Cast<LogicalComparisonJoin>();

		const auto lhs_bindings = comparison_join.children[0]->GetColumnBindings();
		const auto &lhs_types = comparison_join.children[0]->types;

		const auto rhs_bindings = comparison_join.children[1]->GetColumnBindings();
		const auto &rhs_types = comparison_join.children[1]->types;

		// Create a struct_pack expression
		vector<unique_ptr<Expression>> struct_pack_arguments;
		child_list_t<LogicalType> struct_children;
		for (idx_t col_idx = 0; col_idx < rhs_bindings.size(); ++col_idx) {
			const auto &type = rhs_types[col_idx];
			const auto &binding = rhs_bindings[col_idx];
			const auto col_name = StringUtil::Format("c%llu", col_idx);
			auto colref = make_uniq<BoundColumnRefExpression>(type, binding);
			colref->alias = col_name;
			struct_pack_arguments.push_back(std::move(colref));
			struct_children.emplace_back(col_name, type);
		}
		auto struct_pack_fun = StructPackFun::GetFunction();
		auto struct_pack_bind_info = struct_pack_fun.bind(input.context, struct_pack_fun, struct_pack_arguments);
		auto struct_pack_expr = make_uniq<BoundFunctionExpression>(
		    LogicalType::STRUCT(std::move(struct_children)), std::move(struct_pack_fun),
		    std::move(struct_pack_arguments), std::move(struct_pack_bind_info));

		// Create a struct_to_sparse_variant expression
		vector<unique_ptr<Expression>> struct_to_sparse_variant_arguments;
		struct_to_sparse_variant_arguments.push_back(std::move(struct_pack_expr));
		auto struct_to_sparse_variant_expr = make_uniq<BoundFunctionExpression>(
		    LogicalType::VARIANT(), FivetranFunctions::GetStructToSparseVariantFunction(),
		    std::move(struct_to_sparse_variant_arguments), nullptr);

		// Create a projection for the build side of the join
		const auto projection_in_table_index = input.optimizer.binder.GenerateTableIndex();
		vector<unique_ptr<Expression>> projection_in_expressions;
		for (auto &condition : comparison_join.conditions) {
			D_ASSERT(condition.right->GetExpressionClass() == ExpressionClass::BOUND_COLUMN_REF);
			projection_in_expressions.push_back(condition.right->Copy());

			// Also fix up column bindings in the join condition
			auto &colref = condition.right->Cast<BoundColumnRefExpression>();
			colref.binding = {projection_in_table_index, projection_in_expressions.size() - 1};
		}
		const ColumnBinding variant_column_binding = {projection_in_table_index, projection_in_expressions.size()};
		projection_in_expressions.push_back(std::move(struct_to_sparse_variant_expr));
		auto projection_in =
		    make_uniq<LogicalProjection>(projection_in_table_index, std::move(projection_in_expressions));

		// Replace the build side input with the projection
		projection_in->children.push_back(std::move(comparison_join.children[1]));
		comparison_join.children[1] = std::move(projection_in);

		// Create a projection on top of the join. First we get LHS columns
		const auto mapped_lhs_bindings =
		    LogicalOperator::MapBindings(lhs_bindings, comparison_join.left_projection_map);
		const auto mapped_lhs_types = LogicalOperator::MapTypes(lhs_types, comparison_join.left_projection_map);
		vector<unique_ptr<Expression>> projection_out_expressions;
		for (idx_t lhs_col_idx = 0; lhs_col_idx < mapped_lhs_bindings.size(); lhs_col_idx++) {
			projection_out_expressions.push_back(
			    make_uniq<BoundColumnRefExpression>(mapped_lhs_types[lhs_col_idx], mapped_lhs_bindings[lhs_col_idx]));
		}

		// Get RHS columns from the VARIANT
		const auto variant_extract_fun_set = VariantExtractFun::GetFunctions();
		const auto actual_rhs_cols = comparison_join.right_projection_map.empty()
		                                 ? rhs_bindings.size()
		                                 : comparison_join.right_projection_map.size();
		for (idx_t i = 0; i < actual_rhs_cols; i++) {
			const auto col_idx =
			    comparison_join.right_projection_map.empty() ? i : comparison_join.right_projection_map[i];
			// Create a variant_extract expression
			auto variant_extract_fun = variant_extract_fun_set.functions[0];
			vector<unique_ptr<Expression>> variant_extract_arguments;
			variant_extract_arguments.push_back(
			    make_uniq<BoundColumnRefExpression>(LogicalType::VARIANT(), variant_column_binding));
			variant_extract_arguments.push_back(
			    make_uniq<BoundConstantExpression>(StringUtil::Format("c%llu", col_idx)));
			auto variant_extract_bind_info =
			    variant_extract_fun.bind(input.context, variant_extract_fun, variant_extract_arguments);
			auto variant_extract_expr = make_uniq<BoundFunctionExpression>(
			    LogicalType::VARIANT(), std::move(variant_extract_fun), std::move(variant_extract_arguments),
			    std::move(variant_extract_bind_info));

			// Create a TRY expression
			auto try_expr = make_uniq<BoundOperatorExpression>(ExpressionType::OPERATOR_TRY, LogicalType::VARIANT());
			try_expr->children.push_back(std::move(variant_extract_expr));

			// Create a CAST expression
			auto cast_expr =
			    BoundCastExpression::AddCastToType(input.context, std::move(std::move(try_expr)), rhs_types[col_idx]);

			projection_out_expressions.push_back(std::move(cast_expr));
		}

		// This now needs to be cleared since we're not building on many payload columns anymore (just one VARIANT)
		comparison_join.right_projection_map.clear();

		// Place the projection on top of the plan
		auto projection_out = make_uniq<LogicalProjection>(input.optimizer.binder.GenerateTableIndex(),
		                                                   std::move(projection_out_expressions));
		projection_out->children.push_back(std::move(plan));
		plan = std::move(projection_out);
	}

	static void ReplaceBindings(unique_ptr<LogicalOperator> &plan, LogicalOperator &root,
	                            const vector<ColumnBinding> &bindings_before) {
		const auto bindings_after = plan->GetColumnBindings();
		D_ASSERT(bindings_before.size() == bindings_after.size());
		ColumnBindingReplacer replacer;
		for (idx_t i = 0; i < bindings_before.size(); i++) {
			replacer.replacement_bindings.emplace_back(bindings_before[i], bindings_after[i]);
		}
		replacer.stop_operator = plan.get();
		replacer.VisitOperator(root);
	}

public:
	static void Optimize(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
		plan->ResolveOperatorTypes();
		OptimizeInternal(input, plan, *plan);
	}
};

OptimizerExtension FivetranOptimizers::GetSparseBuildOptimizer() {
	return SparseBuildOptimizer();
}

} // namespace duckdb
