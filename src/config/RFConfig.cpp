#include "config/RFConfig.h"

const char* const TAG = "Config::RFConfig";

#include "Common.h"
#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;

RFConfig::RFConfig() : txPin(OPENSHOCK_RF_TX_GPIO), keepAliveEnabled(true) { }

RFConfig::RFConfig(uint8_t txPin, bool keepAliveEnabled) : txPin(txPin), keepAliveEnabled(keepAliveEnabled) { }

void RFConfig::ToDefault() {
  txPin            = OPENSHOCK_RF_TX_GPIO;
  keepAliveEnabled = true;
}

bool RFConfig::FromFlatbuffers(const Serialization::Configuration::RFConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  txPin            = config->tx_pin();
  keepAliveEnabled = config->keepalive_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::RFConfig> RFConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateRFConfig(builder, txPin, keepAliveEnabled);
}

bool RFConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonU8(txPin, json, "txPin", OPENSHOCK_RF_TX_GPIO);
  Internal::Utils::FromJsonBool(keepAliveEnabled, json, "keepAliveEnabled", true);

  return true;
}

cJSON* RFConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "txPin", txPin);  //-V2564
  cJSON_AddBoolToObject(root, "keepAliveEnabled", keepAliveEnabled);

  return root;
}
