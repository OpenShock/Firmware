#pragma once

// The `estop` config section: whether the E-Stop input is used and on which pin. Owned by device_control and kept in
// NVS through the config framework (config/Section.h); NVS namespace "estop", keys enabled (u8) and gpio (i8).

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

  [[nodiscard]] EStopConfig GetEStopConfig();
  bool SetEStopConfig(const EStopConfig& config);
  [[nodiscard]] bool GetEStopEnabled();
  bool SetEStopEnabled(bool enabled);
  [[nodiscard]] gpio_num_t GetEStopGpioPin();
  bool SetEStopGpioPin(gpio_num_t gpioPin);
}  // namespace OpenShock::Config
