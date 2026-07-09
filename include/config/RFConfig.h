#pragma once

#include <hal/gpio_types.h>

#include "config/ConfigBase.h"

namespace OpenShock::Config {
  struct RFConfig : public ConfigBase<Serialization::Configuration::RFConfig> {
    RFConfig();
    RFConfig(gpio_num_t txPin, bool keepAliveEnabled);

    gpio_num_t txPin;
    bool keepAliveEnabled;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::RFConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::RFConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(JSON::JsonView json) override;
    void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
