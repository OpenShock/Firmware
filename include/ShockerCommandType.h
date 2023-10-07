#pragma once

#include <cstdint>

namespace OpenShock {

  enum ShockerCommandType : std::uint8_t {
    Stop    = 0,
    Shock   = 1,
    Vibrate = 2,
    Sound   = 3,
  };

}  // namespace OpenShock
