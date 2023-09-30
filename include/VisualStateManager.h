#pragma once

#include "WiFiState.h"

#include <cstdint>

namespace OpenShock::VisualStateManager {
  void SetCriticalError();
  void SetWiFiState(WiFiState state);
}  // namespace OpenShock::VisualStateManager
