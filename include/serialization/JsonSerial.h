#pragma once

#include "ShockerModelType.h"
#include "ShockerCommandType.h"

#include <cJSON.h>

#include <cstdint>

namespace OpenShock::Serialization::JsonSerial {
  struct ShockerCommand {
    OpenShock::ShockerModelType model;
    std::uint16_t id;
    OpenShock::ShockerCommandType command;
    std::uint8_t intensity;
    std::uint16_t durationMs;
  };

  bool ParseShockerCommand(const cJSON* root, ShockerCommand& out);
}  // namespace OpenShock::Serialization::JsonAPI
