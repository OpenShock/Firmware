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

    bool FromJSON(JSON::JsonView json) override;
    void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
