#pragma once

#include "config/ConfigBase.h"

#include <string>

namespace OpenShock::Config {
  struct OtaUpdateConfig : public ConfigBase<Serialization::Configuration::OtaUpdateConfig> {
    OtaUpdateConfig();
    OtaUpdateConfig(bool isEnabled, std::string cdnDomain, std::string updateChannel, bool checkOnStartup, std::uint16_t checkInterval, bool allowBackendManagement, bool requireManualApproval);

    bool isEnabled;
    std::string cdnDomain;
    std::string updateChannel;
    bool checkOnStartup;
    std::uint16_t checkInterval;
    bool allowBackendManagement;
    bool requireManualApproval;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::OtaUpdateConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::OtaUpdateConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON() const override;
  };
}  // namespace OpenShock::Config
