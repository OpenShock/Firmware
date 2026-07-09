#include "config/BackendConfig.h"

const char* const TAG = "Config::BackendConfig";

#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;

BackendConfig::BackendConfig()
  : domain(CONFIG_OPENSHOCK_API_DOMAIN)
  , authToken()
{
}

BackendConfig::BackendConfig(std::string_view domain, std::string_view authToken)
  : domain(domain)
  , authToken(authToken)
{
}

void BackendConfig::ToDefault()
{
  domain = CONFIG_OPENSHOCK_API_DOMAIN;
  authToken.clear();
}

bool BackendConfig::FromFlatbuffers(const Serialization::Configuration::BackendConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  Internal::Utils::FromFbsStr(domain, config->domain(), CONFIG_OPENSHOCK_API_DOMAIN);
  Internal::Utils::FromFbsStr(authToken, config->auth_token(), "");

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::BackendConfig> BackendConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  auto domainOffset = builder.CreateString(domain);

  flatbuffers::Offset<flatbuffers::String> authTokenOffset;
  if (withSensitiveData) {
    authTokenOffset = builder.CreateString(authToken);
  } else {
    authTokenOffset = 0;
  }

  return Serialization::Configuration::CreateBackendConfig(builder, domainOffset, authTokenOffset);
}

bool BackendConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonStr(domain, json, "domain", CONFIG_OPENSHOCK_API_DOMAIN);
  Internal::Utils::FromJsonStr(authToken, json, "authToken", "");

  return true;
}

void BackendConfig::ToJSON(json_gen_str_t* gen, bool withSensitiveData) const
{
  JSON::objSetString(gen, "domain", domain);

  if (withSensitiveData) {
    JSON::objSetString(gen, "authToken", authToken);
  }
}
