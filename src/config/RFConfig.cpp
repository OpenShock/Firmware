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

  Internal::Utils::FromU8GpioNum(txPin, config->tx_pin(), static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO));
  keepAliveEnabled = config->keepalive_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::RFConfig> RFConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateRFConfig(builder, txPin, keepAliveEnabled);
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

  Internal::Utils::FromJsonGpioNum(txPin, json, "txPin", static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO));
  Internal::Utils::FromJsonBool(keepAliveEnabled, json, "keepAliveEnabled", true);

  return true;
}

void RFConfig::ToJSON(json_gen_str_t* gen, bool withSensitiveData) const
{
  json_gen_obj_set_int(gen, "txPin", static_cast<int>(txPin));
  json_gen_obj_set_bool(gen, "keepAliveEnabled", keepAliveEnabled);
}
