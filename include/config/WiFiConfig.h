#pragma once

#include "config/ConfigBase.h"
#include "config/WiFiCredentials.h"

#include <string>
#include <vector>

namespace OpenShock::Config {
  struct WiFiConfig : public ConfigBase<Serialization::Configuration::WiFiConfig> {
    WiFiConfig();
    WiFiConfig(const std::string& accessPointSSID, const std::string& hostname, const std::vector<WiFiCredentials>& credentialsList);

    std::string accessPointSSID;
    std::string hostname;
    std::vector<OpenShock::Config::WiFiCredentials> credentialsList;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::WiFiConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::WiFiConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
