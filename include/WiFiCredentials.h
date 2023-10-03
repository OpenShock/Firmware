#pragma once

#include <nonstd/span.hpp>

#include <esp_wifi_types.h>

#include <array>
#include <vector>
#include <cstdint>

namespace fs { class File; }
class String;

namespace OpenShock {
  class WiFiCredentials {
    WiFiCredentials() = default;
  public:
    static bool Load(std::vector<WiFiCredentials>& credentials);

    WiFiCredentials(std::uint8_t id, const wifi_ap_record_t* record);
    WiFiCredentials(std::uint8_t id, const String& ssid, const String& password);

    constexpr std::uint8_t id() const noexcept {
      return _id;
    }

    constexpr nonstd::span<const char> ssid() const noexcept {
      return nonstd::span<const char>(reinterpret_cast<const char*>(_ssid), _ssidLength);
    }
    constexpr nonstd::span<const std::uint8_t> ssidBytes() const noexcept {
      return nonstd::span<const std::uint8_t>(_ssid, _ssidLength);
    }
    void setSSID(const String& ssid);
    void setSSID(const std::uint8_t* ssid, std::size_t ssidLength);

    constexpr nonstd::span<const char> password() const noexcept {
      return nonstd::span<const char>(reinterpret_cast<const char*>(_password), _passwordLength);
    }
    constexpr nonstd::span<const std::uint8_t> passwordBytes() const noexcept {
      return nonstd::span<const std::uint8_t>(_password, _passwordLength);
    }
    void setPassword(const String& password);
    void setPassword(const std::uint8_t* password, std::size_t passwordLength);

    bool save() const;
    bool erase() const;
  private:
    bool _load(fs::File& file);

    std::size_t _ssidLength;
    std::size_t _passwordLength;
    std::uint8_t _password[64];
    std::uint8_t _ssid[33];
    std::uint8_t _id;
  };
}
