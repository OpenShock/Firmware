#include "WiFiNetwork.h"

#include "Utils/HexUtils.h"

#include <cstring>

OpenShock::WiFiNetwork::WiFiNetwork() : channel(0), rssi(0), authMode(WiFiAuthMode::UNKNOWN), credentialsID(0), connectAttempts(0), lastConnectAttempt(0), scansMissed(0) {
  memset(ssid, 0, sizeof(ssid));
  memset(bssid, 0, sizeof(bssid));
}

OpenShock::WiFiNetwork::WiFiNetwork(const char (&ssid)[33], const std::uint8_t (&bssid)[6], std::uint8_t channel, std::int8_t rssi, WiFiAuthMode authMode, std::uint8_t credentialsId)
  : channel(channel), rssi(rssi), authMode(authMode), credentialsID(credentialsId), connectAttempts(0), lastConnectAttempt(0), scansMissed(0) {
  static_assert(sizeof(ssid) == sizeof(this->ssid) && sizeof(ssid) == 33, "SSID buffers must be 33 bytes long! (32 bytes for the SSID + 1 byte for the null terminator)");
  static_assert(sizeof(bssid) == sizeof(this->bssid) && sizeof(bssid) == 6, "BSSIDs must be 6 bytes long!");

  memcpy(this->ssid, ssid, sizeof(ssid));
  memcpy(this->bssid, bssid, sizeof(bssid));
}

OpenShock::WiFiNetwork::WiFiNetwork(const std::uint8_t (&ssid)[33], const std::uint8_t (&bssid)[6], std::uint8_t channel, std::int8_t rssi, WiFiAuthMode authMode, std::uint8_t credentialsId)
  : WiFiNetwork(reinterpret_cast<const char (&)[33]>(ssid), bssid, channel, rssi, authMode, credentialsId) { }

std::array<char, 18> OpenShock::WiFiNetwork::GetHexBSSID() const {
  return OpenShock::HexUtils::ToHexMac<6>(bssid);
}

bool OpenShock::WiFiNetwork::IsSaved() const {
  return credentialsID != 0;
}
