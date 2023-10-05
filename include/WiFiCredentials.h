#pragma once

#include <nonstd/span.hpp>

#include <esp_wifi_types.h>

#include <array>
#include <cstdint>
#include <vector>

namespace fs {
  class File;
}

namespace OpenShock {
  class WiFiCredentials {
    WiFiCredentials() = default;

  public:
    static bool Load(std::vector<WiFiCredentials>& credentials);

    WiFiCredentials(std::uint8_t id, const char* ssid, std::uint8_t ssidLength, const char* password, std::uint8_t passwordLength);

    constexpr std::uint8_t id() const noexcept { return _id; }

    constexpr nonstd::span<const std::uint8_t, 6> bssid() const noexcept { return nonstd::span<const std::uint8_t, 6>(_bssid); }
    void setBSSID(const std::uint8_t* bssid);

    constexpr bool hasSSID() const noexcept { return _ssidLength > 0; }
    constexpr nonstd::span<const char> ssid() const noexcept { return nonstd::span<const char>(_ssid, _ssidLength); }
    void setSSID(const char* ssid, std::uint8_t ssidLength);

    constexpr bool hasPassword() const noexcept { return _passwordLength > 0; }
    constexpr nonstd::span<const char> password() const noexcept { return nonstd::span<const char>(_password, _passwordLength); }
    void setPassword(const char* password, std::uint8_t passwordLength);

    bool save() const;
    bool erase() const;

  private:
    bool _load(fs::File& file);

    std::uint8_t _id;
    std::uint8_t _bssid[6];
    char _ssid[33];
    std::uint8_t _ssidLength;
    char _password[64];
    std::uint8_t _passwordLength;
  };
}  // namespace OpenShock
