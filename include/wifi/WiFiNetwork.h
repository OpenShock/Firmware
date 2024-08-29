#pragma once

#include <esp_wifi_types.h>

#include <array>
#include <cstdint>

namespace OpenShock {
  struct WiFiNetwork {
    WiFiNetwork();
    WiFiNetwork(const wifi_ap_record_t* apRecord, uint8_t credentialsId);
    WiFiNetwork(const char (&ssid)[33], const uint8_t (&bssid)[6], uint8_t channel, int8_t rssi, wifi_auth_mode_t authMode, uint8_t credentialsId);
    WiFiNetwork(const uint8_t (&ssid)[33], const uint8_t (&bssid)[6], uint8_t channel, int8_t rssi, wifi_auth_mode_t authMode, uint8_t credentialsId);

    std::array<char, 18> GetHexBSSID() const;
    bool IsSaved() const;

    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    wifi_auth_mode_t authMode;
    uint8_t credentialsID;
    uint16_t connectAttempts;  // TODO: Add connectSuccesses as well, so we can track the success rate of a network
    int64_t lastConnectAttempt;
    uint8_t scansMissed;
  };
}  // namespace OpenShock
