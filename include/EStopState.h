#pragma once

#include <cstdint>

namespace OpenShock {
  enum class EStopState : uint8_t {
    Idle,
    Active,
    ActiveClearing,
    AwaitingRelease
  };
}
