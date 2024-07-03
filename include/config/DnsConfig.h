#pragma once

#include "config/ConfigBase.h"
#include "StringView.h"

#include <IPAddress.h>

#include <string>
#include <vector>

namespace OpenShock::Config {
  struct DnsConfig : public ConfigBase<Serialization::Configuration::DnsConfig> {
    DnsConfig();
    DnsConfig(bool useDhcp, IPAddress primary, IPAddress secondary, IPAddress fallback);

    bool useDhcp;
    IPAddress primary;
    IPAddress secondary;
    IPAddress fallback;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::DnsConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::DnsConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
