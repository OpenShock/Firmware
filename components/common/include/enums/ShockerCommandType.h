#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  enum class ShockerCommandType : uint8_t {
    Stop,
    Shock,
    Vibrate,
    Sound,
    Light
  };

  [[nodiscard]] inline std::optional<ShockerCommandType> TryParseShockerCommandType(std::string_view str)
  {
    if (StringIEquals(str, "stop")) {
      return ShockerCommandType::Stop;
    } else if (StringIEquals(str, "shock")) {
      return ShockerCommandType::Shock;
    } else if (StringIEquals(str, "vibrate")) {
      return ShockerCommandType::Vibrate;
    } else if (StringIEquals(str, "sound")) {
      return ShockerCommandType::Sound;
    } else if (StringIEquals(str, "light")) {
      return ShockerCommandType::Light;
    } else {
      return std::nullopt;
    }
  }
}  // namespace OpenShock
