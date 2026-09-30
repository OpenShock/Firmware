#pragma once

#include "enums/ShockerCommandType.h"
#include "enums/ShockerModelType.h"
#include "json/Json.h"

#include <cstdint>

namespace OpenShock::Serialization::JsonSerial {
  struct ShockerCommand {
    OpenShock::ShockerModelType model;
    uint16_t id;
    OpenShock::ShockerCommandType command;
    uint8_t intensity;
    uint16_t durationMs;
  };

  bool ParseShockerCommand(JSON::JsonView root, ShockerCommand& out);
}  // namespace OpenShock::Serialization::JsonSerial
