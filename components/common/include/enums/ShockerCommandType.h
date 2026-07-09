#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <string_view>

namespace OpenShock {
  enum class ShockerCommandType : uint8_t {
    Stop,
    Shock,
    Vibrate,
    Sound,
    Light
  };

  inline bool TryParseShockerCommandType(ShockerCommandType& out, std::string_view str)
  {
    if (StringIEquals(str, "stop")) {
      out = ShockerCommandType::Stop;
      return true;
    } else if (StringIEquals(str, "shock")) {
      out = ShockerCommandType::Shock;
      return true;
    } else if (StringIEquals(str, "vibrate")) {
      out = ShockerCommandType::Vibrate;
      return true;
    } else if (StringIEquals(str, "sound")) {
      out = ShockerCommandType::Sound;
      return true;
    } else if (StringIEquals(str, "light")) {
      out = ShockerCommandType::Light;
      return true;
    } else {
      return false;
    }
  }
}  // namespace OpenShock
