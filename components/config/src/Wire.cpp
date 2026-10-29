#include <freertos/FreeRTOS.h>

#include "config/Wire.h"

const char* const TAG = "Config::Wire";

#include "config/Parse.h"
#include "config/Section.h"
#include "config/Store.h"
#include "Logging.h"
#include "serialization/VerifiedRoot.h"

#include "json/Json.h"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

using namespace OpenShock;
namespace Fbs = OpenShock::Serialization::Configuration;

flatbuffers::Offset<Fbs::HubConfig> Config::GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData)
{
  // Every sub-table is finished before the HubConfig table starts, as flatbuffers requires
  std::vector<std::pair<flatbuffers::voffset_t, flatbuffers::uoffset_t>> fields;
  for (auto* section = SectionBase::First(); section != nullptr; section = section->Next()) {
    if (section->Info().hubConfigField != 0) {
      fields.emplace_back(section->Info().hubConfigField, section->ToFlatbuffers(builder, withSensitiveData));
    }
  }

  const flatbuffers::uoffset_t start = builder.StartTable();
  for (const auto& [field, offset] : fields) {
    builder.AddOffset(field, flatbuffers::Offset<void>(offset));
  }
  return flatbuffers::Offset<Fbs::HubConfig>(builder.EndTable(start));
}

bool Config::GetRaw(TinyVec<uint8_t>& buffer)
{
  // Write every field, even ones equal to the schema default. Older firmware may use different defaults (1.5.x reads a
  // missing is_enabled/keepalive_enabled as false), so an omitted field changes meaning when the dump is loaded there.
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  Fbs::FinishHubConfigBuffer(builder, GetAsFlatBuffer(builder, true));

  buffer.resize(builder.GetSize());
  memcpy(buffer.data(), builder.GetBufferPointer(), builder.GetSize());
  return true;
}

bool Config::SetRaw(const uint8_t* buffer, std::size_t size)
{
  if (buffer == nullptr) {
    return false;
  }
  auto json = HubConfigToJSON(std::span<const uint8_t>(buffer, size));
  if (!json) {
    OS_LOGE(TAG, "Not a valid HubConfig (%zu bytes)", size);
    return false;
  }
  return SaveFromJSON(*json);
}

// ---------------------------------------------------------------------------
// HubConfig -> jsonconfig document. Emits, per section, exactly what the section's own FromJSON reads, so a
// HubConfig and the JSON it converts to configure a hub the same way.
// ---------------------------------------------------------------------------

namespace {
  // Whether `field` is physically present in `table` (flatbuffers omits a scalar equal to its schema default). The
  // generated tables derive privately from flatbuffers::Table, which is only a view of the same bytes.
  bool isStored(const void* table, flatbuffers::voffset_t field)
  {
    return table != nullptr && static_cast<const flatbuffers::Table*>(table)->CheckField(field);
  }

  // Whether `field` is present in `table` with a non-zero value, ignoring the schema default.
  bool storedTrue(const void* table, flatbuffers::voffset_t field)
  {
    return isStored(table, field) && static_cast<const flatbuffers::Table*>(table)->GetField<uint8_t>(field, 0) != 0;
  }

  void setString(json_gen_str_t* gen, const char* name, const flatbuffers::String* value)
  {
    if (value != nullptr) {
      JSON::objSetString(gen, name, std::string_view(value->c_str(), value->size()));
    }
  }

  struct LegacyBools {
    bool keepAliveEnabled;
    bool otaEnabled;
    bool allowBackendManagement;
  };

  // Up to 1.5.x the schema declared rf.keepalive_enabled, ota_update.is_enabled and ota_update.allow_backend_management
  // with default false; since 1.6 they default to true. Flatbuffers omits a field equal to its default, so a field
  // omitted by a 1.5.x hub (false) reads back as true under the current schema.
  //
  // Which convention wrote a buffer shows only when at least one of the three is stored: a stored true can only come
  // from the old schema (1.6+ omits true, and the 2.x wire form stores all three, so it omits none). When that is the
  // case, the omitted ones meant false. A buffer that omits all three is ambiguous (1.5.x with all three off, or 1.6+
  // with all three on) and keeps the current schema's default (true).
  LegacyBools resolveFlippedDefaults(const Fbs::HubConfig* config)
  {
    const Fbs::RFConfig* rf         = config->rf();
    const Fbs::OtaUpdateConfig* ota = config->ota_update();

    LegacyBools out {
      .keepAliveEnabled       = rf != nullptr ? rf->keepalive_enabled() : true,
      .otaEnabled             = ota != nullptr ? ota->is_enabled() : true,
      .allowBackendManagement = ota != nullptr ? ota->allow_backend_management() : true,
    };

    const bool legacyWriter = storedTrue(rf, Fbs::RFConfig::VT_KEEPALIVE_ENABLED) || storedTrue(ota, Fbs::OtaUpdateConfig::VT_IS_ENABLED) || storedTrue(ota, Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT);
    if (legacyWriter) {
      if (rf != nullptr && !isStored(rf, Fbs::RFConfig::VT_KEEPALIVE_ENABLED)) out.keepAliveEnabled = false;
      if (ota != nullptr && !isStored(ota, Fbs::OtaUpdateConfig::VT_IS_ENABLED)) out.otaEnabled = false;
      if (ota != nullptr && !isStored(ota, Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT)) out.allowBackendManagement = false;
    }
    return out;
  }

  void writeRf(json_gen_str_t* gen, const Fbs::RFConfig* rf, const LegacyBools& bools)
  {
    if (rf == nullptr) {
      return;
    }
    json_gen_push_object(gen, "rf");
    json_gen_obj_set_int(gen, "txPin", rf->tx_pin());  // RFConfig::FromJSON maps "no pin" and invalid pins to the board's
    json_gen_obj_set_bool(gen, "keepAliveEnabled", bools.keepAliveEnabled);
    json_gen_pop_object(gen);
  }

  void writeWifi(json_gen_str_t* gen, const Fbs::WiFiConfig* wifi)
  {
    if (wifi == nullptr) {
      return;
    }
    json_gen_push_object(gen, "wifi");
    setString(gen, "accessPointSSID", wifi->ap_ssid());
    setString(gen, "hostname", wifi->hostname());
    json_gen_push_array(gen, "credentials");  // Required by WiFiConfig::FromJSON whenever "wifi" is given
    if (const auto* list = wifi->credentials()) {
      for (const Fbs::WiFiCredentials* creds : *list) {
        if (creds == nullptr) {
          continue;
        }
        json_gen_start_object(gen);
        json_gen_obj_set_int(gen, "id", creds->id());
        setString(gen, "ssid", creds->ssid());  // A credential without an SSID is dropped by WiFiCredentials::FromJSON
        setString(gen, "password", creds->password());
        json_gen_obj_set_int(gen, "authMode", std::to_underlying(creds->auth_mode()));
        if (const auto* bssid = creds->bssid()) {
          static constexpr char kHex[] = "0123456789ABCDEF";
          char hex[13];
          for (int i = 0; i < 6; ++i) {
            const uint8_t b = bssid->bytes()->Get(i);
            hex[i * 2]      = kHex[b >> 4];
            hex[i * 2 + 1]  = kHex[b & 0x0F];
          }
          hex[12] = '\0';
          JSON::objSetString(gen, "bssid", hex);
        }
        json_gen_end_object(gen);
      }
    }
    json_gen_pop_array(gen);
    json_gen_pop_object(gen);
  }

  void writeCaptivePortal(json_gen_str_t* gen, const Fbs::CaptivePortalConfig* portal)
  {
    if (portal == nullptr) {
      return;
    }
    json_gen_push_object(gen, "captivePortal");
    json_gen_obj_set_bool(gen, "alwaysEnabled", portal->always_enabled());
    json_gen_pop_object(gen);
  }

  void writeBackend(json_gen_str_t* gen, const Fbs::BackendConfig* backend)
  {
    if (backend == nullptr) {
      return;
    }
    json_gen_push_object(gen, "backend");
    setString(gen, "domain", backend->domain());
    setString(gen, "authToken", backend->auth_token());
    json_gen_pop_object(gen);
  }

  void writeSerialInput(json_gen_str_t* gen, const Fbs::SerialInputConfig* serial)
  {
    if (serial == nullptr) {
      return;
    }
    json_gen_push_object(gen, "serialInput");
    json_gen_obj_set_bool(gen, "echoEnabled", serial->echo_enabled());
    json_gen_pop_object(gen);
  }

  void writeOtaUpdate(json_gen_str_t* gen, const Fbs::OtaUpdateConfig* ota, const LegacyBools& bools)
  {
    if (ota == nullptr) {
      return;
    }
    json_gen_push_object(gen, "otaUpdate");
    json_gen_obj_set_bool(gen, "isEnabled", bools.otaEnabled);
    setString(gen, "cdnDomain", ota->cdn_domain());
    // Out-of-range values (bit flips, configs from newer firmware) have no name and are left out: the default
    if (const char* channel = Fbs::EnumNameOtaUpdateChannel(ota->update_channel()); channel != nullptr && channel[0] != '\0') {
      JSON::objSetString(gen, "updateChannel", channel);
    }
    json_gen_obj_set_bool(gen, "checkOnStartup", ota->check_on_startup());
    json_gen_obj_set_bool(gen, "checkPeriodically", ota->check_periodically());
    json_gen_obj_set_int(gen, "checkInterval", ota->check_interval());
    json_gen_obj_set_bool(gen, "allowBackendManagement", bools.allowBackendManagement);
    json_gen_obj_set_bool(gen, "requireManualApproval", ota->require_manual_approval());
    json_gen_obj_set_int(gen, "updateId", ota->update_id());
    if (const char* step = Fbs::EnumNameOtaUpdateStep(ota->update_step()); step != nullptr && step[0] != '\0') {
      JSON::objSetString(gen, "updateStep", step);
    }
    json_gen_pop_object(gen);
  }

  void writeEStop(json_gen_str_t* gen, const Fbs::EStopConfig* estop)
  {
    if (estop == nullptr) {
      return;
    }
    // A pin that is neither "no pin" nor a real GPIO means a corrupt section: left out, so it takes its defaults
    if (!Config::Parse::GpioNum(estop->gpio_pin())) {
      OS_LOGW(TAG, "Invalid E-Stop GPIO pin %d, using the default E-Stop config", estop->gpio_pin());
      return;
    }
    json_gen_push_object(gen, "estop");
    json_gen_obj_set_bool(gen, "enabled", estop->enabled());
    json_gen_obj_set_int(gen, "gpioPin", estop->gpio_pin());
    json_gen_pop_object(gen);
  }
}  // namespace

std::optional<std::string> Config::HubConfigToJSON(std::span<const uint8_t> buffer)
{
  if (buffer.empty() || buffer.size() > kMaxHubConfigSize) {
    OS_LOGE(TAG, "HubConfig has an impossible size (%zu bytes)", buffer.size());
    return std::nullopt;
  }
  const Fbs::HubConfig* config = Serialization::GetVerifiedRoot<Fbs::HubConfig>(buffer, kMaxHubConfigSize);
  if (config == nullptr) {
    OS_LOGE(TAG, "HubConfig failed verification (%zu bytes)", buffer.size());
    return std::nullopt;
  }

  const LegacyBools bools = resolveFlippedDefaults(config);

  JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  writeRf(gen, config->rf(), bools);
  writeWifi(gen, config->wifi());
  writeCaptivePortal(gen, config->captive_portal());
  writeBackend(gen, config->backend());
  writeSerialInput(gen, config->serial_input());
  writeOtaUpdate(gen, config->ota_update(), bools);
  writeEStop(gen, config->estop());
  json_gen_end_object(gen);

  std::string json = writer.finish();
  if (writer.failed()) {
    OS_LOGE(TAG, "HubConfig holds a string that is not valid UTF-8");
    return std::nullopt;
  }
  return json;
}
