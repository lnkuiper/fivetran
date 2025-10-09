//===----------------------------------------------------------------------===//
//                         DuckDB
//
// settings.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"

namespace duckdb {

struct SparseBuildOptimizerColumnsThresholdSetting {
	static constexpr int64_t DEFAULT_VALUE = 10;
	static constexpr LogicalTypeId TYPE = LogicalTypeId::BIGINT;
	static constexpr const char *NAME = "fivetran_sparse_build_optimizer_column_threshold";
	static constexpr const char *DESCRIPTION = "Number of build columns at which to consider using the "
	                                           "SparseBuildOptimizer. Set to a negative value to disable.";
};

} // namespace duckdb
