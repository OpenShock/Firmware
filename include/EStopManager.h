#pragma once

#include "EStopState.h"

#include <hal/gpio_types.h>

#include <cstdint>

namespace OpenShock::EStopManager {
  bool Init();
  bool SetEStopEnabled(bool enabled);
  bool SetEStopPin(gpio_num_t pin);
  bool IsEStopped();
  int64_t LastEStopped();
}  // namespace OpenShock::EStopManager
