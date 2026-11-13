#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  // Which states the status LEDs show
  enum class LedMode : uint8_t {
    All,           // Every state
    CriticalOnly,  // Dark during normal operation; only an E-Stop or a critical error lights them
    Off            // Always dark
  };

  enum class LedType : uint8_t {
    Mono,  // The single-color built-in LED
    Rgb    // The WS2812B RGB LED
  };

  // What a pattern signals: critical is an E-Stop or a critical error, normal is everything else
  enum class LedSignal : uint8_t {
    Critical,
    Normal
  };

  // The name jsonconfig and the serial console use; TryParseLedMode reads it back.
  [[nodiscard]] constexpr const char* LedModeName(LedMode mode)
  {
    switch (mode) {
      case LedMode::All:
        return "all";
      case LedMode::CriticalOnly:
        return "critical";
      case LedMode::Off:
        return "off";
    }
    return "";
  }

  [[nodiscard]] inline std::optional<LedMode> TryParseLedMode(std::string_view str)
  {
    if (StringIEquals(str, "all")) {
      return LedMode::All;
    }
    if (StringIEquals(str, "critical")) {
      return LedMode::CriticalOnly;
    }
    if (StringIEquals(str, "off")) {
      return LedMode::Off;
    }
    return std::nullopt;
  }

  [[nodiscard]] inline std::optional<LedType> TryParseLedType(std::string_view str)
  {
    if (StringIEquals(str, "mono")) {
      return LedType::Mono;
    }
    if (StringIEquals(str, "rgb")) {
      return LedType::Rgb;
    }
    return std::nullopt;
  }

  [[nodiscard]] inline std::optional<LedSignal> TryParseLedSignal(std::string_view str)
  {
    if (StringIEquals(str, "critical")) {
      return LedSignal::Critical;
    }
    if (StringIEquals(str, "normal")) {
      return LedSignal::Normal;
    }
    return std::nullopt;
  }
}  // namespace OpenShock
