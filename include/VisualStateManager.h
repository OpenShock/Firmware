#pragma once

#include "EStopManager.h"
#include <cstdint>

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  void SetCriticalError();
  void SetScanningStarted();
}  // namespace OpenShock::VisualStateManager
