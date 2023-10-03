#pragma once

#include "WiFiState.h"

#include <WString.h>

#include <nonstd/span.hpp>

#include <cstdint>

namespace OpenShock::WiFiManager {
  bool Init();

  bool Authenticate(nonstd::span<std::uint8_t, 6> bssid, const String& password);
  void Forget(std::uint8_t wifiId);

  void Connect(std::uint8_t wifiId);
  void Disconnect();
} // namespace OpenShock::WiFiManager
