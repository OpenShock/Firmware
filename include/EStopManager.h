#pragma once

#include "EStopState.h"

#include <hal/gpio_types.h>

#include <cstdint>

namespace OpenShock::EStopManager {
  [[nodiscard]] bool Init();
  bool SetEStopEnabled(bool enabled);
  bool SetEStopPin(gpio_num_t pin);
  bool IsEStopped();
  int64_t LastEStopped();

  void Trigger();
}  // namespace OpenShock::EStopManager
