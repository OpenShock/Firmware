#pragma once

#include "WiFiState.h"

#include <cstdint>

namespace OpenShock::WiFiManager {
  bool Init();

  WiFiState GetWiFiState();

  void AddOrUpdateNetwork(const char* ssid, const char* password);
  void RemoveNetwork(const char* ssid);

  bool StartScan();

}  // namespace OpenShock::WiFiManager
