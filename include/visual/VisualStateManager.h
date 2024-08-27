#pragma once

#include "EStopManager.h"
#include <cstdint>

namespace OpenShock::VisualStateManager {
  bool Init();

  void SetCriticalError();
  void SetScanningStarted();
  void SetEmergencyStopStatus(bool isActive, bool isAwaitingRelease);
  void SetWebSocketConnected(bool isConnected);
}  // namespace OpenShock::VisualStateManager
