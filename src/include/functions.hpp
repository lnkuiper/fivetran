//===----------------------------------------------------------------------===//
//                         DuckDB
//
// functions.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

struct StructToSparseVariantFun {
	static constexpr const char *Name = "struct_to_sparse_variant";
	static constexpr const char *Parameters = "struct";
	static constexpr const char *Description = "Convert a STRUCT to a sparse VARIANT, without NULLs";
	static constexpr const char *Example = "struct_to_sparse_variant({duck: 42, goose: NULL})";
	static constexpr const char *Categories = "";

	static ScalarFunction GetFunction();
};

} // namespace duckdb
