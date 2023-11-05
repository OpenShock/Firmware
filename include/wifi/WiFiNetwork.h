#pragma once

#include <esp_wifi_types.h>

#include <array>
#include <cstdint>

namespace OpenShock {
  struct WiFiNetwork {
    WiFiNetwork();
    WiFiNetwork(const char (&ssid)[33], const std::uint8_t (&bssid)[6], std::uint8_t channel, std::int8_t rssi, wifi_auth_mode_t authMode, std::uint8_t credentialsId);
    WiFiNetwork(const std::uint8_t (&ssid)[33], const std::uint8_t (&bssid)[6], std::uint8_t channel, std::int8_t rssi, wifi_auth_mode_t authMode, std::uint8_t credentialsId);

    std::array<char, 18> GetHexBSSID() const;
    bool IsSaved() const;

    char ssid[33];
    std::uint8_t bssid[6];
    std::uint8_t channel;
    std::int8_t rssi;
    wifi_auth_mode_t authMode;
    std::uint8_t credentialsID;
    std::uint16_t connectAttempts;
    std::int64_t lastConnectAttempt;
    std::uint8_t scansMissed;
  };
}  // namespace OpenShock
