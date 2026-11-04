#pragma once

#include "enums/StatusLed.h"
#include "util/FunctionRef.h"
#include "util/PercentUtils.h"
#include "visual/LedOverrides.h"  // Previews and unsaved brightness

#include <cstdint>

namespace OpenShock::Config {
  struct LedConfig;
}

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  /// @brief Whether this board has an LED of `type`. False for both before Init.
  [[nodiscard]] bool HasLed(LedType type);
  /// @brief Whether this board has any status LED.
  [[nodiscard]] inline bool HasAnyLed()
  {
    return HasLed(LedType::Mono) || HasLed(LedType::Rgb);
  }

  // The setters save to the `led` config section and apply the change at once. A save that raises an RGB brightness
  // above kRgbBrightnessWarn logs a warning: a WS2812B is glaring and runs warm well before full output.

  inline constexpr uint16_t kRgbBrightnessWarn = PercentUtils::OfMax<uint16_t, 30.0>();

  [[nodiscard]] LedMode GetLedMode();
  bool SetLedMode(LedMode mode);

  /// @brief 16-bit, 0-65535 of the LED's full output, for patterns of `signal`. The drivers reduce it to what the LED
  ///        shows (10 bits on the dithered RGB LED).
  [[nodiscard]] uint16_t GetLedBrightness(LedType type, LedSignal signal);
  bool SetLedBrightness(LedType type, LedSignal signal, uint16_t brightness);

  /// @brief Several LED settings in one config write (Config::UpdateLedConfig).
  bool UpdateLedConfig(Util::FunctionRef<bool(Config::LedConfig&)> update);

  void SetCriticalError();
  void SetScanningStarted();

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s. Ends every override, and turns new ones
  /// down until it is done.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
