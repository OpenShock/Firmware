#pragma once

#include "config/ConfigBase.h"
#include "StringView.h"

#include <string>

namespace OpenShock::Config {
  struct BackendConfig : public ConfigBase<Serialization::Configuration::BackendConfig> {
    BackendConfig();
    BackendConfig(StringView domain, StringView authToken, StringView lcgOverride);

    std::string domain;
    std::string authToken;
    std::string lcgOverride;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::BackendConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::BackendConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
