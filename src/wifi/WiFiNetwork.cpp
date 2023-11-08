#include "wifi/WiFiNetwork.h"

#include "Utils/HexUtils.h"

#include <cstring>

using namespace OpenShock;

WiFiNetwork::WiFiNetwork() : channel(0), rssi(0), authMode(WIFI_AUTH_MAX), credentialsID(0), connectAttempts(0), lastConnectAttempt(0), scansMissed(0) {
  memset(ssid, 0, sizeof(ssid));
  memset(bssid, 0, sizeof(bssid));
}

WiFiNetwork::WiFiNetwork(const wifi_ap_record_t* apRecord, std::uint8_t credentialsId)
  : channel(apRecord->primary), rssi(apRecord->rssi), authMode(apRecord->authmode), credentialsID(credentialsId), connectAttempts(0), lastConnectAttempt(0), scansMissed(0) {
  static_assert(sizeof(ssid) == sizeof(apRecord->ssid) && sizeof(ssid) == 33, "SSID buffers must be 33 bytes long! (32 bytes for the SSID + 1 byte for the null terminator)");
  static_assert(sizeof(bssid) == sizeof(apRecord->bssid) && sizeof(bssid) == 6, "BSSIDs must be 6 bytes long!");

  memcpy(ssid, apRecord->ssid, sizeof(ssid));
  memcpy(bssid, apRecord->bssid, sizeof(bssid));
}

WiFiNetwork::WiFiNetwork(const char (&ssid)[33], const std::uint8_t (&bssid)[6], std::uint8_t channel, std::int8_t rssi, wifi_auth_mode_t authMode, std::uint8_t credentialsId)
  : channel(channel), rssi(rssi), authMode(authMode), credentialsID(credentialsId), connectAttempts(0), lastConnectAttempt(0), scansMissed(0) {
  static_assert(sizeof(ssid) == sizeof(this->ssid) && sizeof(ssid) == 33, "SSID buffers must be 33 bytes long! (32 bytes for the SSID + 1 byte for the null terminator)");
  static_assert(sizeof(bssid) == sizeof(this->bssid) && sizeof(bssid) == 6, "BSSIDs must be 6 bytes long!");

  memcpy(this->ssid, ssid, sizeof(ssid));
  memcpy(this->bssid, bssid, sizeof(bssid));
}

WiFiNetwork::WiFiNetwork(const std::uint8_t (&ssid)[33], const std::uint8_t (&bssid)[6], std::uint8_t channel, std::int8_t rssi, wifi_auth_mode_t authMode, std::uint8_t credentialsId)
  : WiFiNetwork(reinterpret_cast<const char (&)[33]>(ssid), bssid, channel, rssi, authMode, credentialsId) { }

std::array<char, 18> WiFiNetwork::GetHexBSSID() const {
  return HexUtils::ToHexMac<6>(bssid);
}

bool WiFiNetwork::IsSaved() const {
  return credentialsID != 0;
}
