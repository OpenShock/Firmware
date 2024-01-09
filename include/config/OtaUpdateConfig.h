#pragma once

#include "config/ConfigBase.h"
#include "FirmwareBootType.h"
#include "OtaUpdateChannel.h"

#include <string>

namespace OpenShock::Config {
  struct OtaUpdateConfig : public ConfigBase<Serialization::Configuration::OtaUpdateConfig> {
    OtaUpdateConfig();
    OtaUpdateConfig(
      bool isEnabled,
      std::string cdnDomain,
      OtaUpdateChannel updateChannel,
      bool checkOnStartup,
      bool checkPeriodically,
      std::uint16_t checkInterval,
      bool allowBackendManagement,
      bool requireManualApproval,
      std::int32_t updateId,
      FirmwareBootType bootType
    );

    bool isEnabled;
    std::string cdnDomain;
    OtaUpdateChannel updateChannel;
    bool checkOnStartup;
    bool checkPeriodically;
    std::uint16_t checkInterval;
    bool allowBackendManagement;
    bool requireManualApproval;
    std::int32_t updateId;
    FirmwareBootType bootType;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::OtaUpdateConfig* config) override;
    flatbuffers::Offset<Serialization::Configuration::OtaUpdateConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
