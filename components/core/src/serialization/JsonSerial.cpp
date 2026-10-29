#include "serialization/JsonSerial.h"

const char* const TAG = "JsonSerial";

#include "Logging.h"

#include <optional>
#include <string>

using namespace OpenShock::Serialization;

std::optional<JsonSerial::ShockerCommand> JsonSerial::ParseShockerCommand(JSON::JsonView root)
{
  if (!root.isObject()) {
    OS_LOGE(TAG, "not an object");
    return std::nullopt;
  }

  JSON::JsonView model = root["model"];
  if (!model.valid()) {
    OS_LOGE(TAG, "missing 'model' field");
    return std::nullopt;
  }
  auto modelStr = model.tryGetStr();
  if (!modelStr) {
    OS_LOGE(TAG, "value at 'model' is not a string");
    return std::nullopt;
  }
  auto modelType = TryParseShockerModelType(*modelStr);
  if (!modelType) {
    OS_LOGE(TAG, "value at 'model' is not a valid shocker model (caixianlin, petrainer, petrainer998dr, wellturnt330, d80, wellturnt330alt1)");
    return std::nullopt;
  }

  JSON::JsonView id = root["id"];
  if (!id.valid()) {
    OS_LOGE(TAG, "missing 'id' field");
    return std::nullopt;
  }
  auto idInt = id.tryGetI64();
  if (!idInt) {
    OS_LOGE(TAG, "value at 'id' is not a number");
    return std::nullopt;
  }
  if (*idInt < 0 || *idInt > UINT16_MAX) {
    OS_LOGE(TAG, "value at 'id' is out of range (0-65535)");
    return std::nullopt;
  }
  uint16_t idU16 = static_cast<uint16_t>(*idInt);

  JSON::JsonView command = root["type"];
  if (!command.valid()) {
    OS_LOGE(TAG, "missing 'type' field");
    return std::nullopt;
  }
  auto commandStr = command.tryGetStr();
  if (!commandStr) {
    OS_LOGE(TAG, "value at 'type' is not a string");
    return std::nullopt;
  }
  auto commandType = TryParseShockerCommandType(*commandStr);
  if (!commandType) {
    OS_LOGE(TAG, "value at 'type' is not a valid shocker command (shock, vibrate, sound, light, stop)");
    return std::nullopt;
  }

  JSON::JsonView intensity = root["intensity"];
  if (!intensity.valid()) {
    OS_LOGE(TAG, "missing 'intensity' field");
    return std::nullopt;
  }
  auto intensityInt = intensity.tryGetI64();
  if (!intensityInt) {
    OS_LOGE(TAG, "value at 'intensity' is not a number");
    return std::nullopt;
  }
  if (*intensityInt < 0 || *intensityInt > UINT8_MAX) {
    OS_LOGE(TAG, "value at 'intensity' is out of range (0-255)");
    return std::nullopt;
  }
  uint8_t intensityU8 = static_cast<uint8_t>(*intensityInt);

  JSON::JsonView durationMs = root["durationMs"];
  if (!durationMs.valid()) {
    OS_LOGE(TAG, "missing 'durationMs' field");
    return std::nullopt;
  }
  auto durationMsInt = durationMs.tryGetI64();
  if (!durationMsInt) {
    OS_LOGE(TAG, "value at 'durationMs' is not a number");
    return std::nullopt;
  }
  if (*durationMsInt < 0 || *durationMsInt > UINT16_MAX) {
    OS_LOGE(TAG, "value at 'durationMs' is out of range (0-65535)");
    return std::nullopt;
  }
  uint16_t durationMsU16 = static_cast<uint16_t>(*durationMsInt);

  return ShockerCommand {
    .model      = *modelType,
    .id         = idU16,
    .command    = *commandType,
    .intensity  = intensityU8,
    .durationMs = durationMsU16,
  };
}
