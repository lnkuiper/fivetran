#pragma once

// Detect DuckDB v1.5+ (optimizer_extensions moved to OptimizerExtension::Register)
#if __has_include("duckdb/common/column_index_map.hpp")
#define DUCKDB_V15
#endif
