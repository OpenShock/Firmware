#pragma once

// The `led` config section: which states the status LEDs show and how bright they are. Owned by device_control and
// kept in NVS through the config framework (config/Section.h); namespace "led", keys mode, monoCrit, monoNorm, rgbCrit
// and rgbNorm (u8).

#include "config/Section.h"
#include "enums/StatusLed.h"

#include <cstdint>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct LedConfig {
    static constexpr uint8_t kMaxBrightness = 100;  // Brightness is a percent of the LED's full output

    LedConfig();  // The defaults: the one place they are defined
    LedConfig(LedMode mode, uint8_t monoCriticalBrightness, uint8_t monoNormalBrightness, uint8_t rgbCriticalBrightness, uint8_t rgbNormalBrightness);

    LedMode mode;
    uint8_t monoCriticalBrightness;
    uint8_t monoNormalBrightness;
    uint8_t rgbCriticalBrightness;
    uint8_t rgbNormalBrightness;

    [[nodiscard]] uint8_t& Brightness(LedType type, LedSignal signal);
    [[nodiscard]] uint8_t Brightness(LedType type, LedSignal signal) const { return const_cast<LedConfig*>(this)->Brightness(type, signal); }

    /// @brief Caps each brightness at kMaxBrightness. The section runs it on every value it takes in.
    void Normalize();

    /// @brief Reads this section from `partition`; a missing key is its default, a read error fails.
    bool LoadNvs(const char* partition);
    /// @brief Writes the keys that differ from `previous` (every key when null) to `partition`.
    bool SaveNvs(const char* partition, const LedConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  [[nodiscard]] LedConfig GetLedConfig();
  bool SetLedConfig(const LedConfig& config);
  bool SetLedMode(LedMode mode);
  /// @brief `percent` is capped at LedConfig::kMaxBrightness.
  bool SetLedBrightness(LedType type, LedSignal signal, uint8_t percent);
}  // namespace OpenShock::Config
