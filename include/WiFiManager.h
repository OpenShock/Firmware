#pragma once

#include <cstdint>
#include <string>

namespace OpenShock::WiFiManager {
  bool Init();

  bool Authenticate(std::uint8_t (&bssid)[6], const std::string& password);
  void Forget(std::uint8_t wifiId);
  bool IsSaved(const char* ssid);
  bool IsSaved(const std::uint8_t (&bssid)[6]);
  bool IsSaved(const char* ssid, const std::uint8_t (&bssid)[6]);

  void Connect(std::uint8_t wifiId);
  void Disconnect();
}  // namespace OpenShock::WiFiManager
