#pragma once

#include <cstdint>

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  void SetCriticalError();
  void SetScanningStarted();

  /// @brief While set, the LED says not to cut the power (magenta on RGB, mostly-on on a plain LED): the partition
  ///        layout migration's flash writes. A critical error and the E-Stop states still show over it.
  void SetDoNotUnplug(bool active);

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
