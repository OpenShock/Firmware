#pragma once

#include "config/ConfigBase.h"

#include <hal/gpio_types.h>
#include <WiFiType.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace OpenShock::Config {
  struct WiFiCredentials : public ConfigBase<Serialization::Configuration::WiFiCredentials> {
    WiFiCredentials();
    WiFiCredentials(uint8_t id, std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode = WIFI_AUTH_MAX);

    uint8_t id;
    std::string ssid;
    std::string password;
    wifi_auth_mode_t authMode;       // Auth mode when saved, WIFI_AUTH_MAX = unknown
    std::array<uint8_t, 6> bssid;    // Pinned BSSID, all zeros = no pin

    bool HasPinnedBSSID() const;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::WiFiCredentials* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::WiFiCredentials> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(const cJSON* json) override;
    [[nodiscard]] cJSON* ToJSON(bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
