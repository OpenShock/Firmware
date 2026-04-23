#pragma once

#include <cstdint>

namespace OpenShock {
  enum class SetGPIOResultCode : uint8_t {
    Success,
    InvalidPin,
    InternalError
  };
}  // namespace OpenShock
