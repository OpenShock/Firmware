#include "config/RFConfig.h"

#include "Logging.h"

const char* const TAG = "Config::RFConfig";

using namespace OpenShock::Config;

void RFConfig::ToDefault() {
  txPin = 0U;
}

bool RFConfig::FromFlatbuffers(const Serialization::Configuration::RFConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  txPin = config->tx_pin();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::RFConfig> RFConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const {
  return Serialization::Configuration::CreateRFConfig(builder, txPin);
}

bool RFConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (!cJSON_IsObject(json)) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  const cJSON* txPinJson = cJSON_GetObjectItemCaseSensitive(json, "txPin");
  if (!cJSON_IsNumber(txPinJson)) {
    ESP_LOGE(TAG, "value at 'txPin' is not a number");
    return false;
  }

  txPin = txPinJson->valueint;

  return true;
}

cJSON* RFConfig::ToJSON() const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "txPin", txPin);

  return root;
}
