#include "optimizers.hpp"

#include "duckdb/optimizer/optimizer.hpp"
#include "duckdb/optimizer/column_binding_replacer.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"

namespace duckdb {

class SparseBuildOptimizer : public OptimizerExtension {
public:
	SparseBuildOptimizer() {
		optimize_function = Optimize;
	}

private:
	static constexpr idx_t BUILD_COLUMN_THRESHOLD = 10;

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
		if (!TrySparsifyBuild(input, plan)) {
			return;
		}

		ReplaceBindings(plan, root, bindings_before);
	}

	static bool IsEligible(LogicalOperator &op) {
		if (op.type != LogicalOperatorType::LOGICAL_COMPARISON_JOIN) {
			return false;
		}
		if (op.children[1]->types.size() < BUILD_COLUMN_THRESHOLD) {
			return false;
		}
		switch (op.Cast<LogicalComparisonJoin>().join_type) {
		case JoinType::LEFT:
			return true;
		default:
			return false;
		}
	}

	static bool TrySparsifyBuild(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
		D_ASSERT(plan->type == LogicalOperatorType::LOGICAL_COMPARISON_JOIN);
		D_ASSERT(plan->children.size() >= BUILD_COLUMN_THRESHOLD);
		auto &comparison_join = plan->Cast<LogicalComparisonJoin>();
		D_ASSERT(comparison_join.join_type == JoinType::LEFT);
		throw NotImplementedException("TODO");
	}

	static void ReplaceBindings(unique_ptr<LogicalOperator> &plan, LogicalOperator &root,
	                            const vector<ColumnBinding> &bindings_before) {
		const auto bindings_after = plan->GetColumnBindings();
		D_ASSERT(bindings_before.size() == bindings_after.size());
		ColumnBindingReplacer replacer;
		for (idx_t i = 0; i < bindings_before.size(); i++) {
			replacer.replacement_bindings.emplace_back(bindings_after[i], bindings_after[i]);
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