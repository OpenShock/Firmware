#pragma once

#include <cstdint>

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  /// @brief Whether the status LEDs show the hub's state. When off, they only light up for an active E-Stop or a
  ///        critical error.
  [[nodiscard]] bool GetStatusLedsEnabled();
  /// @brief Saves the setting to the `led` config section and applies it at once.
  bool SetStatusLedsEnabled(bool enabled);

  void SetCriticalError();
  void SetScanningStarted();

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
