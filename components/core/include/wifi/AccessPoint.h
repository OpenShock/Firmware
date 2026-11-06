#pragma once

// The captive portal's access point as the wifi config describes it, read one way by everything that shows or starts
// it (the portal, the serial console's `ap`, the Ready message):
//   - the default name is stored as the bare CONFIG_OPENSHOCK_FW_AP_PREFIX and broadcast with the hub's MAC appended
//   - an empty password is an open network, CONFIG_OPENSHOCK_FW_AP_PASSWORD is the build's default (which a factory
//     reset restores), anything else is the user's own

#include <cstdint>
#include <string>
#include <string_view>

namespace OpenShock::AccessPoint {
  enum class Protection : uint8_t {
    Open            = 0,
    DefaultPassword = 1,
    CustomPassword  = 2,
  };

  [[nodiscard]] bool IsDefaultSSID(std::string_view configuredSsid);
  [[nodiscard]] Protection ProtectionOf(std::string_view password);

  /// @brief The default name: the prefix with this hub's STA MAC, so hubs side by side stay apart
  [[nodiscard]] std::string DefaultSSID();
  /// @brief The name the access point broadcasts for a configured SSID: itself, or DefaultSSID() for the default
  [[nodiscard]] std::string BroadcastSSID(std::string_view configuredSsid);
}  // namespace OpenShock::AccessPoint
