#pragma once

#include <Arduino.h>
#include <cstdint>

namespace OpenShock::EStopManager {
  typedef enum
  {
      ALL_CLEAR, // The initial, idle state
      ESTOPPED_AND_HELD, // The EStop has been pressed and has not yet been released
      ESTOPPED, // Idle EStopped state
      ESTOPPED_CLEARED // The EStop has been cleared by the user, but we're waiting for the user to release the button (to avoid incidental estops)
  } EStopStatus_t;

  void Init();
  EStopStatus_t Update();
  bool IsEStopped();
  unsigned long WhenEStopped();
} // namespace OpenShock