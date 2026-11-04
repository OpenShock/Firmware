#pragma once

#include "enums/StatusLed.h"
#include "util/FunctionRef.h"
#include "util/PercentUtils.h"

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
  /// @brief Shows a solid color: the RGB LED in `red`/`green`/`blue` (16 bits per channel, 0-65535; the driver dithers
  ///        what the LED's 8 bits can't show), the mono LED on unless the color is black.
  /// @param only Light only this LED; the other stays dark. All LEDs when unset.
  /// @param brightness 0-65535 for this preview only; each LED's normal brightness when unset.
  void PreviewColor(uint16_t red, uint16_t green, uint16_t blue, std::optional<LedType> only, std::optional<uint16_t> brightness);
  /// @brief Ends a preview early.
  void StopPreview();

  /// Cycles through all LED patterns for visual verification. Blocks for ~20s.
  void RunLedTest();
}  // namespace OpenShock::VisualStateManager
