#pragma once

#include "enums/StatusLed.h"

#include <cstdint>

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  // The setters save to the `led` config section and apply the change at once.

  [[nodiscard]] LedMode GetLedMode();
  bool SetLedMode(LedMode mode);

  /// @brief Percent of the LED's full output (0-100) for patterns of `signal`.
  [[nodiscard]] uint8_t GetLedBrightness(LedType type, LedSignal signal);
  /// @brief `percent` is capped at 100. Logs a warning for an RGB brightness above 30%.
  bool SetLedBrightness(LedType type, LedSignal signal, uint8_t percent);

  void SetCriticalError();
  void SetScanningStarted();

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
