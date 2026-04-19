#pragma once

#include "config/ConfigBase.h"

#include <string>
#include <string_view>

namespace OpenShock::Config {
  struct LanConfig : public ConfigBase<Serialization::Configuration::LanConfig> {
    LanConfig();
    LanConfig(bool apiKeyEnabled, std::string_view apiKey);

    bool apiKeyEnabled;
    std::string apiKey;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::LanConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::LanConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
