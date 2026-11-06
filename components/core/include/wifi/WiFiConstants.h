#pragma once

// The limits 802.11 and WPA put on what the wifi module stores and sends to the driver

#include <esp_wifi_types.h>

#include <cstddef>

namespace OpenShock::WiFiConstants {
  inline constexpr std::size_t kSsidMaxLength       = MAX_SSID_LEN;        // 32 bytes
  inline constexpr std::size_t kPassphraseMinLength = 8;                   // A WPA2/WPA3 passphrase
  inline constexpr std::size_t kPassphraseMaxLength = 63;                  // One more is a raw PSK written as hex
  inline constexpr std::size_t kPskHexLength        = MAX_PASSPHRASE_LEN;  // 64
  inline constexpr std::size_t kMacStringLength     = sizeof("AA:BB:CC:DD:EE:FF") - 1;
}  // namespace OpenShock::WiFiConstants
