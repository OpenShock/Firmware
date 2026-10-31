#pragma once

// The `rf` config section: the radio transmitter's pin and keep-alive. Owned by device_control and kept in NVS
// through the config framework (config/Section.h); NVS namespace "rf", keys tx_pin (i8) and keepalive (u8).

#include "config/Section.h"
#include "serialization/_fbs/HubConfig_generated.h"

#include <hal/gpio_types.h>

#include <optional>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct RFConfig {
    RFConfig();  // The defaults: the one place they are defined
    RFConfig(gpio_num_t txPin, bool keepAliveEnabled);

    gpio_num_t txPin;
    bool keepAliveEnabled;

    /// @brief No rules to apply yet. The section runs it on every value it takes in.
    void Normalize() { }

    bool LoadNvs(const char* partition);
    bool SaveNvs(const char* partition, const RFConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::RFConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;
  };

  /// @brief The section itself, for whole-value reads and read-modify-writes.
  [[nodiscard]] RFConfig GetRFConfig();
  bool SetRFConfig(const RFConfig& config);
  [[nodiscard]] gpio_num_t GetRFConfigTxPin();
  bool SetRFConfigTxPin(gpio_num_t txPin);
  [[nodiscard]] bool GetRFConfigKeepAliveEnabled();
  bool SetRFConfigKeepAliveEnabled(bool enabled);
}  // namespace OpenShock::Config
