#pragma once

// The `estop` config section: whether the E-Stop input is used and on which pin. Owned by device_control and kept in
// NVS through the config framework (config/Section.h); NVS namespace "estop", keys enabled (u8) and gpio (i8).
//
// The same namespace holds the E-Stop latch, `latched` (u8): whether the E-Stop was active when it was last saved, so
// a reboot or power cut does not release it. It is state, not config: not part of the section's value, never in
// jsonconfig or the wire form. Keeping it in the section's namespace moves it with the store; a factory reset (which
// erases the section's namespaces) clears it.

#include "config/Section.h"
#include "serialization/_fbs/HubConfig_generated.h"

#include <hal/gpio_types.h>

#include <cstdint>
#include <optional>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  // The schema's `active` and `latching` fields are not modelled: nothing reads them.
  struct EStopConfig {
    EStopConfig();  // The defaults: the one place they are defined
    EStopConfig(bool enabled, gpio_num_t gpioPin);

    bool enabled;
    gpio_num_t gpioPin;

    /// @brief Disables the E-Stop when its pin can't be used as an input. The section runs it on every value it takes in.
    void Normalize();

    bool LoadNvs(const char* partition);
    bool SaveNvs(const char* partition, const EStopConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::EStopConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;
  };

  Section<EStopConfig>& EStopSection();

  [[nodiscard]] std::optional<EStopConfig> GetEStopConfig();
  bool SetEStopConfig(const EStopConfig& config);
  [[nodiscard]] std::optional<bool> GetEStopEnabled();
  bool SetEStopEnabled(bool enabled);
  [[nodiscard]] std::optional<gpio_num_t> GetEStopGpioPin();
  bool SetEStopGpioPin(gpio_num_t gpioPin);

  /// @brief Whether the stored E-Stop latch is set. A missing key, or a hub whose config only lives in RAM, reads as
  ///        not latched (a fresh hub must start usable); a read error reads as latched.
  [[nodiscard]] bool LoadEStopLatched();

  enum class LatchSave : uint8_t {
    Saved,   // on flash (or nothing to keep: the config only lives in RAM)
    Busy,    // another config writer holds the write lock; nothing was written, try again later
    Failed,  // the write failed
  };

  /// @brief Stores the E-Stop latch. Never waits for another config writer (the layout migration holds the write lock
  ///        across closing NVS), so the E-Stop task can call it between samples. Never call it from an ISR.
  [[nodiscard]] LatchSave SaveEStopLatched(bool latched);
}  // namespace OpenShock::Config
