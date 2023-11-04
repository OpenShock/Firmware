#pragma once

#include "WiFiNetwork.h"

#include <cstdint>
#include <string>

namespace OpenShock::WiFiManager {
  bool Init();

  bool Save(const char* ssid, const std::string& password);
  bool Save(const std::uint8_t (&bssid)[6], const std::string& password);
  bool Forget(const char* ssid);
  bool Forget(const std::uint8_t (&bssid)[6]);

  bool IsSaved(const char* ssid);
  bool IsSaved(const std::uint8_t (&bssid)[6]);
  bool IsSaved(const char* ssid, const std::uint8_t (&bssid)[6]);

  bool Connect(const char* ssid);
  bool Connect(const std::uint8_t (&bssid)[6]);
  void Disconnect();

  bool IsConnected();
  bool GetConnectedNetwork(OpenShock::WiFiNetwork& network);

  void Update();
}  // namespace OpenShock::WiFiManager
