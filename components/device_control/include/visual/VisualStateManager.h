#pragma once

#include "enums/StatusLed.h"
#include "util/FunctionRef.h"

#include <cstdint>
#include <optional>

namespace OpenShock::Config {
  struct LedConfig;
}

namespace OpenShock::VisualStateManager {
  [[nodiscard]] bool Init();

  /// @brief Whether this board has an LED of `type`. False for both before Init.
  [[nodiscard]] bool HasLed(LedType type);
  /// @brief Whether this board has any status LED.
  [[nodiscard]] inline bool HasAnyLed() { return HasLed(LedType::Mono) || HasLed(LedType::Rgb); }

  // The setters save to the `led` config section and apply the change at once. A save that raises an RGB brightness
  // above kRgbBrightnessWarnPercent logs a warning: a WS2812B is glaring and runs warm well before full output.

  inline constexpr uint8_t kRgbBrightnessWarnPercent = 30;

  [[nodiscard]] LedMode GetLedMode();
  bool SetLedMode(LedMode mode);

  /// @brief Percent of the LED's full output (0-100) for patterns of `signal`.
  [[nodiscard]] uint8_t GetLedBrightness(LedType type, LedSignal signal);
  /// @brief `percent` is capped at 100.
  bool SetLedBrightness(LedType type, LedSignal signal, uint8_t percent);

  /// @brief Several LED settings in one config write (Config::UpdateLedConfig).
  bool UpdateLedConfig(Util::FunctionRef<bool(Config::LedConfig&)> update);

  void SetCriticalError();
  void SetScanningStarted();

  // Previews take over the LEDs for a few seconds after the last preview call, then the live state shows again.
  // State events keep being tracked meanwhile. The LED test wins over a preview.

  enum class LedPattern : uint8_t {
    WiFiDisconnected,
    WiFiScanning,
    NoBackend,  // Network up, gateway not connected
    Connected,
    EStopped,
    EStopClearing,
    EStopAwaitingRelease,
    CriticalError,
  };

  inline constexpr uint16_t kPreviewDurationMs = 10'000;  // uint16: the portal gets it as LedSettings.preview_ms

  /// @brief Shows `pattern` whatever the LED mode, at the configured brightness.
  void PreviewPattern(LedPattern pattern);
  /// @brief Shows a solid color: the RGB LED in `red`/`green`/`blue`, the mono LED on unless the color is black.
  /// @param only Light only this LED; the other stays dark. All LEDs when unset.
  /// @param brightness Percent (capped at 100) for this preview only; each LED's normal brightness when unset.
  void PreviewColor(uint8_t red, uint8_t green, uint8_t blue, std::optional<LedType> only, std::optional<uint8_t> brightness);
  /// @brief Ends a preview early.
  void StopPreview();

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
