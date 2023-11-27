#pragma once

#include "EStopManager.h"
#include <cstdint>

namespace OpenShock::VisualStateManager {
  void Init();

  void SetCriticalError();
  void SetScanningStarted();
  void SetEmergencyStop(OpenShock::EStopManager::EStopStatus status);
  void SetWebSocketConnected(bool isConnected);
}  // namespace OpenShock::VisualStateManager
