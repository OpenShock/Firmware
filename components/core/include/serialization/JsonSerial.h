#pragma once

#include "enums/ShockerCommandType.h"
#include "enums/ShockerModelType.h"
#include "json/Json.h"

#include <cstdint>
#include <optional>

namespace OpenShock::Serialization::JsonSerial {
  struct ShockerCommand {
    OpenShock::ShockerModelType model;
    uint16_t id;
    OpenShock::ShockerCommandType command;
    uint8_t intensity;
    uint16_t durationMs;
  };

  [[nodiscard]] std::optional<ShockerCommand> ParseShockerCommand(JSON::JsonView root);
}  // namespace OpenShock::Serialization::JsonSerial
