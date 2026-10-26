#pragma once

#include "serialization/_fbs/HubConfig_generated.h"

#include <json_generator.h>

#include <esp_wifi_types.h>
#include <hal/gpio_types.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  // One saved network: an element of the wifi section's credential list, stored as one blob per credential.
  struct WiFiCredentials {
    WiFiCredentials();
    WiFiCredentials(uint8_t id, std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode = WIFI_AUTH_MAX);

    uint8_t id;
    std::string ssid;
    std::string password;
    wifi_auth_mode_t authMode;     // Auth mode when saved, WIFI_AUTH_MAX = unknown
    std::array<uint8_t, 6> bssid;  // Pinned BSSID, all zeros = no pin

    bool HasPinnedBSSID() const;

    /// @brief The credential as the blob stored under its id in the `wifi` NVS namespace (wifi/WiFiConfig.h).
    [[nodiscard]] std::vector<uint8_t> ToBlob() const;
    /// @brief Decodes a blob written by ToBlob (any version it ever wrote). False on a malformed blob.
    bool FromBlob(std::span<const uint8_t> blob);

    void ToDefault();

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::WiFiCredentials> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };
}  // namespace OpenShock::Config
