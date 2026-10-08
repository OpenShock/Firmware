#pragma once

#include <cstddef>

namespace OpenShock::EthernetManager {
  // Brings up the board's Ethernet PHY. A no-op returning true on boards without Ethernet.
  // Requires esp_netif_init() and the default event loop (call after WiFiManager::Init()).
  bool Init();
  bool IsLinkUp();
  bool HasIPAddress();
  bool GetIPv4(char* out, std::size_t len);
}  // namespace OpenShock::EthernetManager
