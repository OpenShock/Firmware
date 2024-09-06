#include "config/EStopConfig.h"

#include "Chipset.h"
#include "Common.h"
#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::EStopConfig";

using namespace OpenShock::Config;

EStopConfig::EStopConfig() : enabled(OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN)), gpioPin(static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN)) { }

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin) : enabled(enabled), gpioPin(gpioPin) { }

void EStopConfig::ToDefault() {
  enabled = OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN);
  gpioPin = static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN);
}

bool EStopConfig::FromFlatbuffers(const Serialization::Configuration::EStopConfig* config) {
  if (config == nullptr) {
    OS_LOGE(TAG, "config is null");
    return false;
  }

  enabled = config->enabled();

  uint8_t val = config->gpio_pin();

  if (!OpenShock::IsValidInputPin(val)) {
    OS_LOGE(TAG, "Invalid EStop pin: %d", val);
    return false;
  }

  gpioPin = static_cast<gpio_num_t>(val);

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::EStopConfig> EStopConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateEStopConfig(builder, gpioPin);
}

bool EStopConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    OS_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  if (!Internal::Utils::FromJsonBool(enabled, json, "enabled", false)) {
    OS_LOGE(TAG, "Failed to parse enabled");
    return false;
  }

  uint8_t val;
  if (!Internal::Utils::FromJsonU8(val, json, "gpioPin", OPENSHOCK_ESTOP_PIN)) {
    OS_LOGE(TAG, "Failed to parse gpioPin");
    return false;
  }

  if (!OpenShock::IsValidInputPin(val)) {
    OS_LOGE(TAG, "Invalid EStop pin: %d", val);
    return false;
  }

  return true;
}

cJSON* EStopConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "enabled", enabled);
  cJSON_AddNumberToObject(root, "gpioPin", gpioPin);

  return root;
}
