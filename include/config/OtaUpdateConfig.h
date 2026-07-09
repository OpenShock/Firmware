#pragma once

#include "config/ConfigBase.h"
#include "enums/FirmwareBootType.h"
#include "enums/OtaUpdateChannel.h"
#include "enums/OtaUpdateStep.h"

#include <string>

namespace OpenShock::Config {
  struct OtaUpdateConfig : public ConfigBase<Serialization::Configuration::OtaUpdateConfig> {
    OtaUpdateConfig();
    OtaUpdateConfig(
      bool isEnabled, std::string cdnDomain, OtaUpdateChannel updateChannel, bool checkOnStartup, bool checkPeriodically, uint16_t checkInterval, bool allowBackendManagement, bool requireManualApproval, int32_t updateId, OtaUpdateStep updateStep
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

    bool FromJSON(JSON::JsonView json) override;
    void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
