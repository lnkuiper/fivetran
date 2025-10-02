//===----------------------------------------------------------------------===//
//                         DuckDB
//
// optimizers.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/optimizer/optimizer_extension.hpp"

namespace duckdb {

struct FivetranOptimizers {
	static OptimizerExtension GetSparseBuildOptimizer();
};

} // namespace duckdb
