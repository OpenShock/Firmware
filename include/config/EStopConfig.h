#pragma once

#include "config/ConfigBase.h"

#include <hal/gpio_types.h>

namespace OpenShock::Config {
  struct EStopConfig : public ConfigBase<Serialization::Configuration::EStopConfig> {
    EStopConfig();
    EStopConfig(bool enabled, gpio_num_t gpioPin, bool latching, bool active);

    bool enabled;
    gpio_num_t gpioPin;
    bool latching;
    bool active;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::EStopConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::EStopConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
