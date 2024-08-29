#pragma once

#include "config/ConfigBase.h"

namespace OpenShock::Config {
  struct RFConfig : public ConfigBase<Serialization::Configuration::RFConfig> {
    RFConfig();
    RFConfig(uint8_t txPin, bool keepAliveEnabled);

    uint8_t txPin;
    bool keepAliveEnabled;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::RFConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::RFConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
