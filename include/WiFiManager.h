#pragma once

#include <cstdint>
#include <string>

namespace OpenShock::WiFiManager {
  bool Init();

  bool Save(std::uint8_t (&bssid)[6], const std::string& password);
  bool Forget(const std::uint8_t (&bssid)[6]);

  bool IsSaved(const char* ssid);
  bool IsSaved(const std::uint8_t (&bssid)[6]);
  bool IsSaved(const char* ssid, const std::uint8_t (&bssid)[6]);

  bool Connect(const std::uint8_t (&bssid)[6]);
  void Disconnect();

  void Update();
}  // namespace OpenShock::WiFiManager
