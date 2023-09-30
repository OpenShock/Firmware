#pragma once

#include "WiFiState.h"

#include <cstdint>

namespace ShockLink::VisualStateManager {
  void SetCriticalError();
  void SetWiFiState(WiFiState state);
}  // namespace ShockLink::VisualStateManager
