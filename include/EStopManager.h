#pragma once

#include <hal/gpio_types.h>

#include <cstdint>

namespace OpenShock::EStopManager {
  bool Init();
  bool IsEStopped();
  std::int64_t LastEStopped();
}  // namespace OpenShock::EStopManager
