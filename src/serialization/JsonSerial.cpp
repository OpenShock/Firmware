#include "serialization/JsonSerial.h"

const char* const TAG = "JsonSerial";

#include "Logging.h"

using namespace OpenShock::Serialization;

bool JsonSerial::ParseShockerCommand(const cJSON* root, JsonSerial::ShockerCommand& out) {
  if (cJSON_IsObject(root) == 0) {
    OS_LOGE(TAG, "not an object");
    return false;
  }

  const cJSON* model = cJSON_GetObjectItemCaseSensitive(root, "model");
  if (model == nullptr) {
    OS_LOGE(TAG, "missing 'model' field");
    return false;
  }
  if (cJSON_IsString(model) == 0) {
    OS_LOGE(TAG, "value at 'model' is not a string");
    return false;
  }
  ShockerModelType modelType;
  if (!ShockerModelTypeFromString(model->valuestring, modelType)) {
    OS_LOGE(TAG, "value at 'model' is not a valid shocker model (caixianlin, petrainer, petrainer998dr)");
    return false;
  }

  const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
  if (id == nullptr) {
    OS_LOGE(TAG, "missing 'id' field");
    return false;
  }
  if (cJSON_IsNumber(id) == 0) {
    OS_LOGE(TAG, "value at 'id' is not a number");
    return false;
  }
  int idInt = id->valueint;
  if (idInt < 0 || idInt > UINT16_MAX) {
    OS_LOGE(TAG, "value at 'id' is out of range (0-65535)");
    return false;
  }
  uint16_t idU16 = static_cast<uint16_t>(idInt);

  const cJSON* command = cJSON_GetObjectItemCaseSensitive(root, "type");
  if (command == nullptr) {
    OS_LOGE(TAG, "missing 'type' field");
    return false;
  }
  if (cJSON_IsString(command) == 0) {
    OS_LOGE(TAG, "value at 'type' is not a string");
    return false;
  }
  ShockerCommandType commandType;
  if (!ShockerCommandTypeFromString(command->valuestring, commandType)) {
    OS_LOGE(TAG, "value at 'type' is not a valid shocker command (stop, shock, vibrate, sound)");
    return false;
  }

  const cJSON* intensity = cJSON_GetObjectItemCaseSensitive(root, "intensity");
  if (intensity == nullptr) {
    OS_LOGE(TAG, "missing 'intensity' field");
    return false;
  }
  if (cJSON_IsNumber(intensity) == 0) {
    OS_LOGE(TAG, "value at 'intensity' is not a number");
    return false;
  }
  int intensityInt = intensity->valueint;
  if (intensityInt < 0 || intensityInt > UINT8_MAX) {
    OS_LOGE(TAG, "value at 'intensity' is out of range (0-255)");
    return false;
  }
  uint8_t intensityU8 = static_cast<uint8_t>(intensityInt);

  const cJSON* durationMs = cJSON_GetObjectItemCaseSensitive(root, "durationMs");
  if (durationMs == nullptr) {
    OS_LOGE(TAG, "missing 'durationMs' field");
    return false;
  }
  if (cJSON_IsNumber(durationMs) == 0) {
    OS_LOGE(TAG, "value at 'durationMs' is not a number");
    return false;
  }
  if (durationMs->valueint < 0 || durationMs->valueint > UINT16_MAX) {
    OS_LOGE(TAG, "value at 'durationMs' is out of range (0-65535)");
    return false;
  }
  uint16_t durationMsU16 = static_cast<uint16_t>(durationMs->valueint);

  out = {
    .model      = modelType,
    .id         = idU16,
    .command    = commandType,
    .intensity  = intensityU8,
    .durationMs = durationMsU16,
  };

  return true;
}
