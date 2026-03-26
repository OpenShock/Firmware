#pragma once

#include "estop/EStopManager.h"
#include <cstdint>

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  void SetCriticalError();
  void SetScanningStarted();

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
