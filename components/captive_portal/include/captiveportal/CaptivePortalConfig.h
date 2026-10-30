#pragma once

// The `captivePortal` config section. Owned by captive_portal and kept in NVS through the config framework
// (config/Section.h); namespace "portal", key always (u8).

#include "config/Section.h"
#include "serialization/_fbs/HubConfig_generated.h"

#include <optional>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct CaptivePortalConfig {
    CaptivePortalConfig();  // The defaults: the one place they are defined
    CaptivePortalConfig(bool alwaysEnabled);

    bool alwaysEnabled;

    /// @brief No rules to apply yet. The section runs it on every value it takes in.
    void Normalize() { }

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::CaptivePortalConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;

    /// @brief Reads this section from `partition`; a missing key is its default, a read error fails.
    bool LoadNvs(const char* partition);
    /// @brief Writes the keys that differ from `previous` (every key when null) to `partition`.
    bool SaveNvs(const char* partition, const CaptivePortalConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  Section<CaptivePortalConfig>& CaptivePortalSection();

  [[nodiscard]] std::optional<CaptivePortalConfig> GetCaptivePortalConfig();
  bool SetCaptivePortalConfig(const CaptivePortalConfig& config);
}  // namespace OpenShock::Config
