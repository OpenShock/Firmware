#include "serialization/JsonSerial.h"

const char* const TAG = "JsonSerial";

#include "Logging.h"

#include <string>

using namespace OpenShock::Serialization;

bool JsonSerial::ParseShockerCommand(JSON::JsonView root, JsonSerial::ShockerCommand& out)
{
  if (!root.isObject()) {
    OS_LOGE(TAG, "not an object");
    return false;
  }

  JSON::JsonView model = root["model"];
  if (!model.valid()) {
    OS_LOGE(TAG, "missing 'model' field");
    return false;
  }
  std::string_view modelStr;
  if (!model.tryGetStr(modelStr)) {
    OS_LOGE(TAG, "value at 'model' is not a string");
    return false;
  }
  ShockerModelType modelType = ShockerModelType::CaiXianlin;
  if (!ShockerModelTypeFromString(std::string(modelStr).c_str(), modelType)) {
    OS_LOGE(TAG, "value at 'model' is not a valid shocker model (caixianlin, petrainer, petrainer998dr, wellturnt330, d80)");
    return false;
  }

  JSON::JsonView id = root["id"];
  if (!id.valid()) {
    OS_LOGE(TAG, "missing 'id' field");
    return false;
  }
  int64_t idInt;
  if (!id.tryGetI64(idInt)) {
    OS_LOGE(TAG, "value at 'id' is not a number");
    return false;
  }
  if (idInt < 0 || idInt > UINT16_MAX) {
    OS_LOGE(TAG, "value at 'id' is out of range (0-65535)");
    return false;
  }
  uint16_t idU16 = static_cast<uint16_t>(idInt);

  JSON::JsonView command = root["type"];
  if (!command.valid()) {
    OS_LOGE(TAG, "missing 'type' field");
    return false;
  }
  std::string_view commandStr;
  if (!command.tryGetStr(commandStr)) {
    OS_LOGE(TAG, "value at 'type' is not a string");
    return false;
  }
  ShockerCommandType commandType = ShockerCommandType::Stop;
  if (!ShockerCommandTypeFromString(std::string(commandStr).c_str(), commandType)) {
    OS_LOGE(TAG, "value at 'type' is not a valid shocker command (shock, vibrate, sound, light, stop)");
    return false;
  }

  JSON::JsonView intensity = root["intensity"];
  if (!intensity.valid()) {
    OS_LOGE(TAG, "missing 'intensity' field");
    return false;
  }
  int64_t intensityInt;
  if (!intensity.tryGetI64(intensityInt)) {
    OS_LOGE(TAG, "value at 'intensity' is not a number");
    return false;
  }
  if (intensityInt < 0 || intensityInt > UINT8_MAX) {
    OS_LOGE(TAG, "value at 'intensity' is out of range (0-255)");
    return false;
  }
  uint8_t intensityU8 = static_cast<uint8_t>(intensityInt);

  JSON::JsonView durationMs = root["durationMs"];
  if (!durationMs.valid()) {
    OS_LOGE(TAG, "missing 'durationMs' field");
    return false;
  }
  int64_t durationMsInt;
  if (!durationMs.tryGetI64(durationMsInt)) {
    OS_LOGE(TAG, "value at 'durationMs' is not a number");
    return false;
  }
  if (durationMsInt < 0 || durationMsInt > UINT16_MAX) {
    OS_LOGE(TAG, "value at 'durationMs' is out of range (0-65535)");
    return false;
  }
  uint16_t durationMsU16 = static_cast<uint16_t>(durationMsInt);

  out = {
    .model      = modelType,
    .id         = idU16,
    .command    = commandType,
    .intensity  = intensityU8,
    .durationMs = durationMsU16,
  };

  return true;
}
