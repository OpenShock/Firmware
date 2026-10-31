#pragma once

// The `serialInput` config section. Owned by serial_console and kept in NVS through the config framework
// (config/Section.h); namespace "serial", key echo (u8).

#include "config/Section.h"
#include "serialization/_fbs/HubConfig_generated.h"

#include <optional>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct SerialInputConfig {
    SerialInputConfig();  // The defaults: the one place they are defined
    SerialInputConfig(bool echoEnabled);

    bool echoEnabled;

    /// @brief No rules to apply yet. The section runs it on every value it takes in.
    void Normalize() { }

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::SerialInputConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;

    /// @brief Reads this section from `partition`; a missing key is its default, a read error fails.
    bool LoadNvs(const char* partition);
    /// @brief Writes the keys that differ from `previous` (every key when null) to `partition`.
    bool SaveNvs(const char* partition, const SerialInputConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  [[nodiscard]] SerialInputConfig GetSerialInputConfig();
  bool SetSerialInputConfig(const SerialInputConfig& config);
  [[nodiscard]] bool GetSerialInputConfigEchoEnabled();
  bool SetSerialInputConfigEchoEnabled(bool enabled);
}  // namespace OpenShock::Config
