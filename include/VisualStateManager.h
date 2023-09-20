#pragma once

#include "ConnectionState.h"

#include <cstdint>

namespace ShockLink::VisualStateManager {
  void SetCriticalError();
  void SetConnectionState(ConnectionState state);
}  // namespace ShockLink::VisualStateManager
