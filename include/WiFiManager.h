#pragma once

#include "WiFiState.h"

#include <WString.h>

#include <nonstd/span.hpp>

#include <cstdint>

namespace OpenShock::WiFiManager {
  bool Init();

  WiFiState GetWiFiState();

  bool Authenticate(nonstd::span<std::uint8_t, 6> bssid, const String& password);
  void Forget(std::uint8_t wifiId);

  void Connect(std::uint8_t wifiId);
  void Disconnect();

  bool StartScan();
  void StopScan();

}  // namespace OpenShock::WiFiManager
