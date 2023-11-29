#pragma once

#include "config/ConfigBase.h"

#include <string>

namespace OpenShock::Config {
  struct WiFiCredentials : public ConfigBase<Serialization::Configuration::WiFiCredentials> {
    WiFiCredentials();
    WiFiCredentials(std::uint8_t id, const std::string& ssid, const std::string& password);

    std::uint8_t id;
    std::string ssid;
    std::string password;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::WiFiCredentials* config) override;
    flatbuffers::Offset<Serialization::Configuration::WiFiCredentials> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON() const override;
  };
}  // namespace OpenShock::Config
