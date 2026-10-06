#include "config/RFConfig.h"

const char* const TAG = "Config::RFConfig";

#include "config/internal/utils.h"
#include "Logging.h"
#include "OpenShock.h"

using namespace OpenShock::Config;

RFConfig::RFConfig()
  : txPin(static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO))
  , keepAliveEnabled(true)
{
}

RFConfig::RFConfig(gpio_num_t txPin, bool keepAliveEnabled)
  : txPin(txPin)
  , keepAliveEnabled(keepAliveEnabled)
{
}

void RFConfig::ToDefault()
{
  txPin            = static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO);
  keepAliveEnabled = true;
}

bool RFConfig::FromFlatbuffers(const Serialization::Configuration::RFConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (!Internal::Utils::FromU8GpioNum(txPin, config->tx_pin())) {
    txPin = static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO);
  }
  keepAliveEnabled = config->keepalive_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::RFConfig> RFConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  namespace Fbs = Serialization::Configuration;

  // keepalive_enabled is stored even when it equals the schema default, so the value
  // reads back the same under every schema version (up to 1.5.x it defaulted to
  // false) and RootConfig::FromFlatbuffers never has to infer it.
  Fbs::RFConfigBuilder rfBuilder(builder);
  rfBuilder.add_tx_pin(txPin);
  builder.AddElement<uint8_t>(Fbs::RFConfig::VT_KEEPALIVE_ENABLED, static_cast<uint8_t>(keepAliveEnabled));
  return rfBuilder.Finish();
}

bool RFConfig::FromJSON(JSON::JsonView json)
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

  if (!Internal::Utils::FromJsonGpioNum(txPin, json, "txPin")) {
    txPin = static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO);
  }
  if (!json["keepAliveEnabled"].tryGetBool(keepAliveEnabled)) keepAliveEnabled = true;

  return true;
}

void RFConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_int(gen, "txPin", static_cast<int>(txPin));
  json_gen_obj_set_bool(gen, "keepAliveEnabled", keepAliveEnabled);
  JSON::objEnd(gen, name);
}
