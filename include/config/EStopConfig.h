#pragma once

#include "config/ConfigBase.h"

namespace OpenShock::Config {
  struct EStopConfig : public ConfigBase<Serialization::Configuration::EStopConfig> {
    EStopConfig();
    EStopConfig(std::uint8_t estopPin);

    std::uint8_t estopPin;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::EStopConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::EStopConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config