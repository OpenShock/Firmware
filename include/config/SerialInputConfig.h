#pragma once

#include "config/ConfigBase.h"

namespace OpenShock::Config {
  struct SerialInputConfig : public ConfigBase<Serialization::Configuration::SerialInputConfig> {
    SerialInputConfig();
    SerialInputConfig(bool echoEnabled);

    bool echoEnabled;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::SerialInputConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::SerialInputConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
