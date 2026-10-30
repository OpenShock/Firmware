#include <freertos/FreeRTOS.h>

#include "radio/RFConfig.h"

const char* const TAG = "Config::RFConfig";

#include "BoardDescriptor.h"
#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;
namespace Fbs = OpenShock::Serialization::Configuration;

static constexpr const char* kNamespace    = "rf";
static constexpr const char* kKeyTxPin     = "tx_pin";
static constexpr const char* kKeyKeepAlive = "keepalive";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<RFConfig> s_section({.name = "rf", .order = 0, .hubConfigField = Fbs::HubConfig::VT_RF, .namespaces = kNamespaces});

Section<RFConfig>& OpenShock::Config::RFSection()
{
  return s_section;
}

RFConfig::RFConfig()
  : txPin(static_cast<gpio_num_t>(Board::Get().rfTxGpio))
  , keepAliveEnabled(true)
{
}

RFConfig::RFConfig(gpio_num_t txPin, bool keepAliveEnabled)
  : txPin(txPin)
  , keepAliveEnabled(keepAliveEnabled)
{
}

bool RFConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, false);
  if (!handle) {
    *this = RFConfig();
    return true;
  }

  const int8_t boardPin = static_cast<int8_t>(Board::Get().rfTxGpio);
  int8_t storedPin      = boardPin;
  if (!Nvs::Load(*handle, kKeyTxPin, storedPin, boardPin)) {
    return false;
  }
  // "No pin" (-1) is not a usable TX pin either; both fall back to the board default, as FromJSON does.
  if (auto pin = Parse::GpioNum(storedPin); pin && *pin != GPIO_NUM_NC) {
    txPin = *pin;
  } else {
    txPin = static_cast<gpio_num_t>(Board::Get().rfTxGpio);
  }
  return Nvs::Load(*handle, kKeyKeepAlive, keepAliveEnabled, true);
}

bool RFConfig::SaveNvs(const char* partition, const RFConfig* previous) const
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle) {
    return false;
  }
  const int8_t pin     = static_cast<int8_t>(txPin);
  const int8_t prevPin = previous != nullptr ? static_cast<int8_t>(previous->txPin) : 0;
  return handle->SetI8(kKeyTxPin, pin, previous != nullptr ? &prevPin : nullptr) && handle->SetBool(kKeyKeepAlive, keepAliveEnabled, previous != nullptr ? &previous->keepAliveEnabled : nullptr) && handle->Commit();
}

bool RFConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    *this = RFConfig();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  if (auto pin = Parse::JsonGpioNum(json, "txPin"); pin && *pin != GPIO_NUM_NC) {
    txPin = *pin;
  } else {
    txPin = static_cast<gpio_num_t>(Board::Get().rfTxGpio);
  }
  keepAliveEnabled = json["keepAliveEnabled"].tryGetBool().value_or(true);

  return true;
}

void RFConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  (void)withSensitiveData;
  JSON::objBegin(gen, name);
  json_gen_obj_set_int(gen, "txPin", static_cast<int>(txPin));
  json_gen_obj_set_bool(gen, "keepAliveEnabled", keepAliveEnabled);
  JSON::objEnd(gen, name);
}

flatbuffers::Offset<Fbs::RFConfig> RFConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  (void)withSensitiveData;
  // keepalive_enabled is stored even when it equals the schema default, so the value reads back the same under every
  // schema version (up to 1.5.x it defaulted to false): Config::HubConfigToJSON never has to infer it.
  Fbs::RFConfigBuilder rfBuilder(builder);
  rfBuilder.add_tx_pin(txPin);
  builder.AddElement<uint8_t>(Fbs::RFConfig::VT_KEEPALIVE_ENABLED, static_cast<uint8_t>(keepAliveEnabled));
  return rfBuilder.Finish();
}

std::optional<RFConfig> OpenShock::Config::GetRFConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetRFConfig(const RFConfig& config)
{
  return s_section.Set(config);
}

std::optional<gpio_num_t> OpenShock::Config::GetRFConfigTxPin()
{
  return s_section.Read([](const RFConfig& rf) { return rf.txPin; });
}

bool OpenShock::Config::SetRFConfigTxPin(gpio_num_t txPin)
{
  return s_section.Update([&](RFConfig& rf) {
    rf.txPin = txPin;
    return true;
  });
}

std::optional<bool> OpenShock::Config::GetRFConfigKeepAliveEnabled()
{
  return s_section.Read([](const RFConfig& rf) { return rf.keepAliveEnabled; });
}

bool OpenShock::Config::SetRFConfigKeepAliveEnabled(bool enabled)
{
  return s_section.Update([&](RFConfig& rf) {
    rf.keepAliveEnabled = enabled;
    return true;
  });
}
