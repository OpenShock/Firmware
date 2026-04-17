#pragma once

#include <cstdint>

namespace OpenShock::NetworkManager {
  enum class Interface : uint8_t {
    None     = 0,
    WiFi     = 1,
    Ethernet = 2,
  };

  bool Init();

  // The interface currently providing IP connectivity, or Interface::None when none is up.
  // When both interfaces have an IP, Ethernet wins.
  Interface GetActive();

  // True when any interface has an IP assigned.
  bool HasIP();

  // Per-interface state (both may be true when dual-homed).
  bool IsWiFiConnected();
  bool IsEthernetConnected();
}  // namespace OpenShock::NetworkManager
