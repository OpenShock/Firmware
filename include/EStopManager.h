#pragma once

#include <cstdint>

namespace OpenShock::EStopManager {
  void Init();
  bool IsEStopped();
  int64_t LastEStopped();
}  // namespace OpenShock::EStopManager
