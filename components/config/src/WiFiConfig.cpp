#include "config/WiFiConfig.h"

const char* const TAG = "Config::WiFiConfig";

#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;

WiFiConfig::WiFiConfig()
  : accessPointSSID(CONFIG_OPENSHOCK_FW_AP_PREFIX)
  , hostname(CONFIG_OPENSHOCK_FW_HOSTNAME)
  , credentialsList()
{
}

WiFiConfig::WiFiConfig(std::string_view accessPointSSID, std::string_view hostname, const std::vector<WiFiCredentials>& credentialsList)
  : accessPointSSID(accessPointSSID)
  , hostname(hostname)
  , credentialsList(credentialsList)
{
}

void WiFiConfig::ToDefault()
{
  accessPointSSID = CONFIG_OPENSHOCK_FW_AP_PREFIX;
  hostname        = CONFIG_OPENSHOCK_FW_HOSTNAME;
  credentialsList.clear();
}

bool WiFiConfig::FromFlatbuffers(const Serialization::Configuration::WiFiConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  Internal::Utils::FromFbsStr(accessPointSSID, config->ap_ssid(), CONFIG_OPENSHOCK_FW_AP_PREFIX);
  Internal::Utils::FromFbsStr(hostname, config->hostname(), CONFIG_OPENSHOCK_FW_HOSTNAME);
  Internal::Utils::FromFbsVec(credentialsList, config->credentials());

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiConfig> WiFiConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  std::vector<flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiCredentials>> fbsCredentialsList;
  fbsCredentialsList.reserve(credentialsList.size());

  for (auto& credentials : credentialsList) {
    fbsCredentialsList.push_back(credentials.ToFlatbuffers(builder, withSensitiveData));
  }

  return Serialization::Configuration::CreateWiFiConfig(builder, builder.CreateString(accessPointSSID), builder.CreateString(hostname), builder.CreateVector(fbsCredentialsList));
}

bool WiFiConfig::FromJSON(JSON::JsonView json)
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

  Internal::Utils::FromJsonStr(accessPointSSID, json, "accessPointSSID", CONFIG_OPENSHOCK_FW_AP_PREFIX);
  Internal::Utils::FromJsonStr(hostname, json, "hostname", CONFIG_OPENSHOCK_FW_HOSTNAME);

  JSON::JsonView credentialsListJson = json["credentials"];
  if (!credentialsListJson.valid()) {
    OS_LOGE(TAG, "credentials is null");
    return false;
  }

  if (!credentialsListJson.isArray()) {
    OS_LOGE(TAG, "credentials is not an array");
    return false;
  }

  Internal::Utils::FromJsonArray(credentialsList, credentialsListJson);

  return true;
}

void WiFiConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  JSON::objSetString(gen, "accessPointSSID", accessPointSSID);
  JSON::objSetString(gen, "hostname", hostname);

  json_gen_push_array(gen, "credentials");
  for (auto& credentials : credentialsList) {
    credentials.ToJSON(gen, nullptr, withSensitiveData);
  }
  json_gen_pop_array(gen);
  JSON::objEnd(gen, name);
}
