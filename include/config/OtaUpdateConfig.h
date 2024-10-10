#pragma once

#include "config/ConfigBase.h"
#include "FirmwareBootType.h"
#include "ota/OtaUpdateChannel.h"
#include "ota/OtaUpdateStep.h"

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
      uint16_t checkInterval,
      bool allowBackendManagement,
      bool requireManualApproval,
      int32_t updateId,
      OtaUpdateStep updateStep
    );

    bool isEnabled;
    std::string cdnDomain;
    OtaUpdateChannel updateChannel;
    bool checkOnStartup;
    bool checkPeriodically;
    uint16_t checkInterval;
    bool allowBackendManagement;
    bool requireManualApproval;
    int32_t updateId;
    OtaUpdateStep updateStep;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::OtaUpdateConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::OtaUpdateConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
