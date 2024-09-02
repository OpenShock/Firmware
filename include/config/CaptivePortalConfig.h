#pragma once

#include "config/ConfigBase.h"

namespace OpenShock::Config {
  struct CaptivePortalConfig : public ConfigBase<Serialization::Configuration::CaptivePortalConfig> {
    CaptivePortalConfig();
    CaptivePortalConfig(bool alwaysEnabled);

    bool alwaysEnabled;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::CaptivePortalConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::CaptivePortalConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
