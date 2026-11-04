#pragma once

// The `led` config section: which states the status LEDs show and how bright they are. Owned by device_control and
// kept in NVS through the config framework (config/Section.h); namespace "led", keys mode, monoCrit, monoNorm, rgbCrit
// and rgbNorm (u16).

#include "config/Section.h"
#include "enums/StatusLed.h"
#include "util/FunctionRef.h"

#include <cstdint>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct LedConfig {
    LedConfig();  // The defaults: the one place they are defined
    LedConfig(LedMode mode, uint16_t monoCriticalBrightness, uint16_t monoNormalBrightness, uint16_t rgbCriticalBrightness, uint16_t rgbNormalBrightness);

    LedMode mode;
    // 16-bit, 0-65535 of the LED's full output
    uint16_t monoCriticalBrightness;
    uint16_t monoNormalBrightness;
    uint16_t rgbCriticalBrightness;
    uint16_t rgbNormalBrightness;

    [[nodiscard]] uint16_t& Brightness(LedType type, LedSignal signal);
    [[nodiscard]] uint16_t Brightness(LedType type, LedSignal signal) const { return const_cast<LedConfig*>(this)->Brightness(type, signal); }

    /// @brief Resets a mode that is none of the LedMode values to the default; every uint16 is a valid brightness. The
    ///        section runs it on every value it takes in, whoever the caller.
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
  /// @brief Read-modify-write of the section (config/Section.h Update).
  bool UpdateLedConfig(Util::FunctionRef<bool(LedConfig&)> update);
}  // namespace OpenShock::Config
