#include "config/EStopConfig.h"

#include "Common.h"
#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::EStopConfig";

using namespace OpenShock::Config;

EStopConfig::EStopConfig() : estopPin(OPENSHOCK_ESTOP_DEFAULT) { }

EStopConfig::EStopConfig(std::uint8_t estopPin) : estopPin(estopPin) { }

void EStopConfig::ToDefault() {
  estopPin = OPENSHOCK_ESTOP_DEFAULT;
}

bool EStopConfig::FromFlatbuffers(const Serialization::Configuration::EStopConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  estopPin = config->estop_pin();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::EStopConfig> EStopConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateEStopConfig(builder, estopPin);
}

bool EStopConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonU8(estopPin, json, "estopPin", OPENSHOCK_ESTOP_DEFAULT);

  return true;
}

cJSON* EStopConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "estopPin", estopPin);

  return root;
}
