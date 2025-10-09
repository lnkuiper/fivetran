#define DUCKDB_EXTENSION_MAIN

#include "fivetran_extension.hpp"
#include "functions.hpp"
#include "optimizers.hpp"
#include "settings.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

template <class SETTING>
void AddSetting(DBConfig &config) {
	config.AddExtensionOption(SETTING::NAME, SETTING::DESCRIPTION, SETTING::TYPE, Value(SETTING::DEFAULT_VALUE));
}

static void LoadInternal(ExtensionLoader &loader) {
	loader.RegisterFunction(FivetranFunctions::GetStructToSparseVariantFunction());
	loader.GetDatabaseInstance().config.optimizer_extensions.push_back(FivetranOptimizers::GetSparseBuildOptimizer());

	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	AddSetting<SparseBuildOptimizerColumnsThresholdSetting>(config);
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
