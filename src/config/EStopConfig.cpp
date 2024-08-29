#include "config/EStopConfig.h"

#include "Chipset.h"
#include "Common.h"
#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::EStopConfig";

using namespace OpenShock::Config;

EStopConfig::EStopConfig() : estopPin(static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN)) { }

EStopConfig::EStopConfig(gpio_num_t estopPin) : estopPin(estopPin) { }

void EStopConfig::ToDefault() {
  estopPin = static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN);
}

bool EStopConfig::FromFlatbuffers(const Serialization::Configuration::EStopConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  std::uint8_t val = config->estop_pin();

  if (!OpenShock::IsValidInputPin(val)) {
    ESP_LOGE(TAG, "Invalid estopPin: %d", val);
    return false;
  }

  estopPin = static_cast<gpio_num_t>(val);

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

  std::uint8_t val;
  if (!Internal::Utils::FromJsonU8(val, json, "estopPin", OPENSHOCK_ESTOP_PIN)) {
    ESP_LOGE(TAG, "Failed to parse estopPin");
    return false;
  }

  if (!OpenShock::IsValidInputPin(val)) {
    ESP_LOGE(TAG, "Invalid estopPin: %d", val);
    return false;
  }

  return true;
}

cJSON* EStopConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "estopPin", estopPin);

  return root;
}
