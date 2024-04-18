#pragma once

#include "config/ConfigBase.h"
#include "StringView.h"

#include <string>

namespace OpenShock::Config {
  struct WiFiCredentials : public ConfigBase<Serialization::Configuration::WiFiCredentials> {
    WiFiCredentials();
    WiFiCredentials(std::uint8_t id, StringView ssid, StringView password);

    std::uint8_t id;
    std::string ssid;
    std::string password;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::WiFiCredentials* config) override;
    flatbuffers::Offset<Serialization::Configuration::WiFiCredentials> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
