#include "config/DiagnosticsConfig.h"

const char* const TAG = "Config::DiagnosticsConfig";

#include "config/internal/utils.h"
#include "Logging.h"

#include <utility>

using namespace OpenShock::Config;
using namespace std::string_view_literals;

namespace Fbs = OpenShock::Serialization::Configuration;

DiagnosticsConfig::DiagnosticsConfig()
  : coredumpUpload(false)
  , logUploadLevel(DiagnosticsLogLevel::None)
{
}

DiagnosticsConfig::DiagnosticsConfig(bool coredumpUpload, DiagnosticsLogLevel logUploadLevel)
  : coredumpUpload(coredumpUpload)
  , logUploadLevel(logUploadLevel)
{
}

void DiagnosticsConfig::ToDefault()
{
  coredumpUpload = false;
  logUploadLevel = DiagnosticsLogLevel::None;
}

bool DiagnosticsConfig::FromFlatbuffers(const Fbs::DiagnosticsConfig* config)
{
  if (config == nullptr) {
    ToDefault();  // Configs from before this section existed: both uploads off
    return true;
  }

  coredumpUpload = config->coredump_upload();
  auto level     = std::to_underlying(config->log_upload_level());
  logUploadLevel = level <= std::to_underlying(DiagnosticsLogLevel::Verbose) ? static_cast<DiagnosticsLogLevel>(level) : DiagnosticsLogLevel::None;

  return true;
}

flatbuffers::Offset<Fbs::DiagnosticsConfig> DiagnosticsConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Fbs::CreateDiagnosticsConfig(builder, coredumpUpload, static_cast<Fbs::DiagnosticsLogLevel>(logUploadLevel));
}

bool DiagnosticsConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    ToDefault();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  coredumpUpload = json["coredumpUpload"].tryGetBool().value_or(false);
  Internal::Utils::FromJsonStrParsed(logUploadLevel, json, "logUploadLevel"sv, OpenShock::TryParseDiagnosticsLogLevel, DiagnosticsLogLevel::None);

  return true;
}

void DiagnosticsConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "coredumpUpload", coredumpUpload);
  JSON::objSetString(gen, "logUploadLevel", DiagnosticsLogLevelName(logUploadLevel));
  JSON::objEnd(gen, name);
}
