#pragma once

#include "config/ConfigBase.h"

#include <string>

namespace OpenShock::Config {
  struct BackendConfig : public ConfigBase<Serialization::Configuration::BackendConfig> {
    std::string domain;
    std::string authToken;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::BackendConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::BackendConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON() const override;
  };
}  // namespace OpenShock::Config
