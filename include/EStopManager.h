#pragma once

#include <hal/gpio_types.h>

#include <cstdint>

namespace OpenShock::EStopManager {
  bool Init();
  bool IsEStopped();
  int64_t LastEStopped();
}  // namespace OpenShock::EStopManager
