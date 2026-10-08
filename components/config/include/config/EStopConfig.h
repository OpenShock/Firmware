#pragma once

#include "config/ConfigBase.h"

#include <hal/gpio_types.h>

namespace OpenShock::Config {
  struct EStopConfig : public ConfigBase<Serialization::Configuration::EStopConfig> {
    EStopConfig();
    EStopConfig(bool enabled, gpio_num_t gpioPin);
    EStopConfig(bool enabled, gpio_num_t gpioPin, bool latching, bool active);

    bool enabled;
    gpio_num_t gpioPin;
    bool latching;  // Latching switch (clears when disengaged) instead of a momentary button (hold to clear)
    bool active;    // E-Stop was active when last saved; restored on boot so a reboot can't clear it

    void ToDefault() override;

    /// @brief Disables the E-Stop when its pin can't be used as an input, so every input path stores the same thing.
    void Normalize();

    bool FromFlatbuffers(const Serialization::Configuration::EStopConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::EStopConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(JSON::JsonView json) override;
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
