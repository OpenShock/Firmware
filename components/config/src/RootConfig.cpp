#include "config/RootConfig.h"

const char* const TAG = "Config::RootConfig";

#include "Logging.h"

using namespace OpenShock::Config;

RootConfig::RootConfig()
  : rf()
  , wifi()
  , captivePortal()
  , backend()
  , serialInput()
  , otaUpdate()
  , estop()
{
}

void RootConfig::ToDefault()
{
  rf.ToDefault();
  wifi.ToDefault();
  captivePortal.ToDefault();
  backend.ToDefault();
  serialInput.ToDefault();
  otaUpdate.ToDefault();
  estop.ToDefault();
}

bool RootConfig::FromFlatbuffers(const Serialization::Configuration::HubConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (!rf.FromFlatbuffers(config->rf())) {
    OS_LOGE(TAG, "Unable to load rf config");
    return false;
  }

  if (!wifi.FromFlatbuffers(config->wifi())) {
    OS_LOGE(TAG, "Unable to load wifi config");
    return false;
  }

  if (!captivePortal.FromFlatbuffers(config->captive_portal())) {
    OS_LOGE(TAG, "Unable to load captive portal config");
    return false;
  }

  if (!backend.FromFlatbuffers(config->backend())) {
    OS_LOGE(TAG, "Unable to load backend config");
    return false;
  }

  if (!serialInput.FromFlatbuffers(config->serial_input())) {
    OS_LOGE(TAG, "Unable to load serial input config");
    return false;
  }

  if (!otaUpdate.FromFlatbuffers(config->ota_update())) {
    OS_LOGE(TAG, "Unable to load ota update config");
    return false;
  }

  if (!estop.FromFlatbuffers(config->estop())) {
    OS_LOGE(TAG, "Unable to load estop config");
    return false;
  }

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::HubConfig> RootConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  auto rfOffset            = rf.ToFlatbuffers(builder, withSensitiveData);
  auto wifiOffset          = wifi.ToFlatbuffers(builder, withSensitiveData);
  auto captivePortalOffset = captivePortal.ToFlatbuffers(builder, withSensitiveData);
  auto backendOffset       = backend.ToFlatbuffers(builder, withSensitiveData);
  auto serialInputOffset   = serialInput.ToFlatbuffers(builder, withSensitiveData);
  auto otaUpdateOffset     = otaUpdate.ToFlatbuffers(builder, withSensitiveData);
  auto estopOffset         = estop.ToFlatbuffers(builder, withSensitiveData);

  return Serialization::Configuration::CreateHubConfig(builder, rfOffset, wifiOffset, captivePortalOffset, backendOffset, serialInputOffset, otaUpdateOffset, estopOffset);
}

bool RootConfig::FromJSON(JSON::JsonView json)
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

  if (!rf.FromJSON(json["rf"])) {
    OS_LOGE(TAG, "Unable to load rf config");
    return false;
  }

  if (!wifi.FromJSON(json["wifi"])) {
    OS_LOGE(TAG, "Unable to load wifi config");
    return false;
  }

  if (!captivePortal.FromJSON(json["captivePortal"])) {
    OS_LOGE(TAG, "Unable to load captive portal config");
    return false;
  }

  if (!backend.FromJSON(json["backend"])) {
    OS_LOGE(TAG, "Unable to load backend config");
    return false;
  }

  if (!serialInput.FromJSON(json["serialInput"])) {
    OS_LOGE(TAG, "Unable to load serial input config");
    return false;
  }

  if (!otaUpdate.FromJSON(json["otaUpdate"])) {
    OS_LOGE(TAG, "Unable to load ota update config");
    return false;
  }

  if (!estop.FromJSON(json["estop"])) {
    OS_LOGE(TAG, "Unable to load estop config");
    return false;
  }

  return true;
}

void RootConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  rf.ToJSON(gen, "rf", withSensitiveData);
  wifi.ToJSON(gen, "wifi", withSensitiveData);
  captivePortal.ToJSON(gen, "captivePortal", withSensitiveData);
  backend.ToJSON(gen, "backend", withSensitiveData);
  serialInput.ToJSON(gen, "serialInput", withSensitiveData);
  otaUpdate.ToJSON(gen, "otaUpdate", withSensitiveData);
  estop.ToJSON(gen, "estop", withSensitiveData);
  JSON::objEnd(gen, name);
}
