#define DUCKDB_EXTENSION_MAIN

#include "fivetran_extension.hpp"
#include "functions.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	loader.RegisterFunction(StructToSparseVariantFun::GetFunction());
}

void FivetranExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string FivetranExtension::Name() {
	return "fivetran";
}

std::string FivetranExtension::Version() const {
#ifdef EXT_VERSION_FIVETRAN
	return EXT_VERSION_FIVETRAN;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(fivetran, loader) {
	duckdb::LoadInternal(loader);
}
}
