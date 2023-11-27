#pragma once

#include <cstdint>

namespace OpenShock::EStopManager {
  enum class EStopStatus : std::uint8_t {
    ALL_CLEAR,          // The initial, idle state
    ESTOPPED_AND_HELD,  // The EStop has been pressed and has not yet been released
    ESTOPPED,           // Idle EStopped state
    ESTOPPED_CLEARED    // The EStop has been cleared by the user, but we're waiting for the user to release the button (to avoid incidental estops)
  };

  void Init(std::uint16_t updateIntervalMs);
  bool IsEStopped();
  std::int64_t WhenEStopped();
}  // namespace OpenShock::EStopManager
