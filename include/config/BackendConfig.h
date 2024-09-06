#pragma once

#include "config/ConfigBase.h"

#include <string>
#include <string_view>

namespace OpenShock::Config {
  struct BackendConfig : public ConfigBase<Serialization::Configuration::BackendConfig> {
    BackendConfig();
    BackendConfig(std::string_view domain, std::string_view authToken, std::string_view lcgOverride);

    std::string domain;
    std::string authToken;
    std::string lcgOverride;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::BackendConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::BackendConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
