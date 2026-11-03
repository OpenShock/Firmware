#pragma once

// The `led` config section: whether the status LEDs show the hub's state. Owned by device_control and kept in NVS
// through the config framework (config/Section.h); namespace "led", key status (u8).

#include "config/Section.h"

#include <optional>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct LedConfig {
    LedConfig();  // The defaults: the one place they are defined
    LedConfig(bool statusEnabled);

    // Off: the LEDs stay dark during normal operation and only light up for an active E-Stop or a critical error.
    bool statusEnabled;

    /// @brief No rules to apply yet. The section runs it on every value it takes in.
    void Normalize() { }

    /// @brief Reads this section from `partition`; a missing key is its default, a read error fails.
    bool LoadNvs(const char* partition);
    /// @brief Writes the keys that differ from `previous` (every key when null) to `partition`.
    bool SaveNvs(const char* partition, const LedConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  [[nodiscard]] LedConfig GetLedConfig();
  bool SetLedConfig(const LedConfig& config);
  [[nodiscard]] bool GetLedStatusEnabled();
  bool SetLedStatusEnabled(bool enabled);
}  // namespace OpenShock::Config
