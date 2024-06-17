#pragma once

#include "config/ConfigBase.h"
#include "StringView.h"

#include <IPAddress.h>

#include <string>
#include <vector>

namespace OpenShock::Config {
  struct NetworkConfig : public ConfigBase<Serialization::Configuration::NetworkConfig> {
    NetworkConfig();
    NetworkConfig(IPAddress primaryDNS, IPAddress secondaryDNS, IPAddress fallbackDNS);

    IPAddress primaryDNS;
    IPAddress secondaryDNS;
    IPAddress fallbackDNS;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::NetworkConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::NetworkConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
