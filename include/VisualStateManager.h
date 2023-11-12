#pragma once

#include <cstdint>

namespace OpenShock::VisualStateManager {
  void Init();

  void SetCriticalError();
  void SetScanningStarted();
  void SetEmergencyStop(bool isStopped);
  void SetWebSocketConnected(bool isConnected);
}  // namespace OpenShock::VisualStateManager
