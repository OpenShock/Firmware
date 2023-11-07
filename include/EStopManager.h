#pragma once

#include <Arduino.h>
#include <cstdint>

namespace OpenShock::EStopManager {
  enum class EStopStatus : std::uint8_t {
    ALL_CLEAR,          // The initial, idle state
    ESTOPPED_AND_HELD,  // The EStop has been pressed and has not yet been released
    ESTOPPED,           // Idle EStopped state
    ESTOPPED_CLEARED    // The EStop has been cleared by the user, but we're waiting for the user to release the button (to avoid incidental estops)
  };

  void Init();
  EStopStatus Update();
  bool IsEStopped();
  unsigned long WhenEStopped();
}  // namespace OpenShock::EStopManager
