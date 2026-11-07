#pragma once

// The captive portal's access point as the wifi config describes it, read one way by everything that shows or starts
// it (the portal, the serial console's `ap`, the Ready message):
//   - the default name is stored as the bare CONFIG_OPENSHOCK_FW_AP_PREFIX and broadcast with the hub's MAC appended
//   - an empty password is an open network, CONFIG_OPENSHOCK_FW_AP_PASSWORD is the build's default (which a factory
//     reset restores), anything else is the user's own

#include "StringHelpers.h"
#include "wifi/WiFiConstants.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace OpenShock::AccessPoint {
  enum class Protection : uint8_t {
    Open            = 0,
    DefaultPassword = 1,
    CustomPassword  = 2,
  };

  /// @brief Whether `ssid` can name the access point: 1-32 bytes, no '|' or control characters. The serial console's
  ///        `ap` prints it as a field of a CRLF-framed record, so either would split the response.
  [[nodiscard]] constexpr bool IsValidSSID(std::string_view ssid)
  {
    return !ssid.empty() && ssid.size() <= WiFiConstants::kSsidMaxLength && std::ranges::none_of(ssid, [](char c) { return c == '|' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F; });
  }
  /// @brief Whether `password` can protect it: empty (open), or 8-63 printable ASCII characters for WPA2/WPA3.
  ///        The driver reads it as a C string, so a NUL would cut it short.
  [[nodiscard]] constexpr bool IsValidPassword(std::string_view password)
  {
    return password.empty() || (password.size() >= WiFiConstants::kPassphraseMinLength && password.size() <= WiFiConstants::kPassphraseMaxLength && StringIsPrintableAscii(password));
  }

  [[nodiscard]] bool IsDefaultSSID(std::string_view configuredSsid);
  [[nodiscard]] Protection ProtectionOf(std::string_view password);

  /// @brief The default name: the prefix with this hub's STA MAC, so hubs side by side stay apart
  [[nodiscard]] std::string DefaultSSID();
  /// @brief The name the access point broadcasts for a configured SSID: itself, or DefaultSSID() for the default
  [[nodiscard]] std::string BroadcastSSID(std::string_view configuredSsid);
}  // namespace OpenShock::AccessPoint
