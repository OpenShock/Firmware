#pragma once

#include <cstdint>

namespace OpenShock {
  enum class SetGPIOResultCode : uint8_t {
    Success       = 0,
    InvalidPin    = 1,
    InternalError = 2,
    PinInUse      = 3,
  };
}  // namespace OpenShock
