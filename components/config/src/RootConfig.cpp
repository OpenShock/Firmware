#include "config/RootConfig.h"

const char* const TAG = "Config::RootConfig";

#include "Logging.h"

using namespace OpenShock::Config;

namespace Fbs = OpenShock::Serialization::Configuration;

// Whether `field` is physically present in `table` (flatbuffers omits a scalar equal
// to its schema default). The generated tables derive privately from
// flatbuffers::Table, which is only a view of the same bytes.
static bool isStored(const void* table, flatbuffers::voffset_t field)
{
  return table != nullptr && static_cast<const flatbuffers::Table*>(table)->CheckField(field);
}

// Whether `field` is present in `table` with a non-zero value, ignoring the schema default.
static bool storedTrue(const void* table, flatbuffers::voffset_t field)
{
  return isStored(table, field) && static_cast<const flatbuffers::Table*>(table)->GetField<uint8_t>(field, 0) != 0;
}

// Up to 1.5.x the schema declared rf.keepalive_enabled, ota_update.is_enabled and
// ota_update.allow_backend_management with default false; since 1.6 they default to
// true. Flatbuffers omits a field equal to its default, so a field omitted by a 1.5.x
// hub (false) reads back as true under the current schema.
//
// Which convention wrote a file shows only when at least one of the three is stored:
// a stored true can only come from the old schema (1.6+ omits true, and this firmware
// stores all three, so it omits none). When that is the case, the omitted ones meant
// false. A file that omits all three is ambiguous (1.5.x with all three off, or 1.6+
// with all three on) and keeps the current schema's default (true).
static void applyLegacyBoolDefaults(const Fbs::HubConfig* config, RFConfig& rf, OtaUpdateConfig& otaUpdate)
{
  const Fbs::RFConfig* fbsRf         = config->rf();
  const Fbs::OtaUpdateConfig* fbsOta = config->ota_update();

  const bool legacyWriter = storedTrue(fbsRf, Fbs::RFConfig::VT_KEEPALIVE_ENABLED) || storedTrue(fbsOta, Fbs::OtaUpdateConfig::VT_IS_ENABLED) || storedTrue(fbsOta, Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT);
  if (!legacyWriter) {
    return;
  }

  if (fbsRf != nullptr && !isStored(fbsRf, Fbs::RFConfig::VT_KEEPALIVE_ENABLED)) {
    rf.keepAliveEnabled = false;
  }
  if (fbsOta != nullptr && !isStored(fbsOta, Fbs::OtaUpdateConfig::VT_IS_ENABLED)) {
    otaUpdate.isEnabled = false;
  }
  if (fbsOta != nullptr && !isStored(fbsOta, Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT)) {
    otaUpdate.allowBackendManagement = false;
  }
}

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

  applyLegacyBoolDefaults(config, rf, otaUpdate);

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
