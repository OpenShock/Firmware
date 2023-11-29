#pragma once

#include "config/RFConfig.h"
#include "config/WiFiConfig.h"
#include "config/CaptivePortalConfig.h"
#include "config/BackendConfig.h"
#include "config/ConfigBase.h"

namespace OpenShock::Config {
  struct RootConfig : public ConfigBase<Serialization::Configuration::Config> {
    OpenShock::Config::RFConfig rf;
    OpenShock::Config::WiFiConfig wifi;
    OpenShock::Config::CaptivePortalConfig captivePortal;
    OpenShock::Config::BackendConfig backend;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::Config* config) override;
    flatbuffers::Offset<Serialization::Configuration::Config> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const override;

    bool FromJSON(const cJSON* json) override;
    cJSON* ToJSON() const override;
  };
}  // namespace OpenShock::Config
