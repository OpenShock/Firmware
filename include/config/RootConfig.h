#pragma once

#include "config/BackendConfig.h"
#include "config/CaptivePortalConfig.h"
#include "config/ConfigBase.h"
#include "config/EStopConfig.h"
#include "config/OtaUpdateConfig.h"
#include "config/RFConfig.h"
#include "config/SerialInputConfig.h"
#include "config/WiFiConfig.h"

namespace OpenShock::Config {
  struct RootConfig : public ConfigBase<Serialization::Configuration::HubConfig> {
    OpenShock::Config::RFConfig rf;
    OpenShock::Config::EStopConfig estop;
    OpenShock::Config::WiFiConfig wifi;
    OpenShock::Config::CaptivePortalConfig captivePortal;
    OpenShock::Config::BackendConfig backend;
    OpenShock::Config::SerialInputConfig serialInput;
    OpenShock::Config::OtaUpdateConfig otaUpdate;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::HubConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::HubConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
