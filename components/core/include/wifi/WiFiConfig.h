#pragma once

// The `wifi` config section: the soft-AP SSID and password, the hostname and the saved networks. Owned by core
// (WiFiManager) and kept in NVS through the config framework (config/Section.h):
//   namespace "wifi.cfg": ap_ssid (str), ap_pass (str), hostname (str)
//   namespace "wifi":     one blob per credential, key = its id in decimal ("1".."255"); enumerated, never counted

#include "config/Section.h"
#include "util/FunctionRef.h"
#include "wifi/WiFiCredentials.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock::Config {
  struct WiFiConfig {
    WiFiConfig();  // The defaults: the one place they are defined
    WiFiConfig(std::string_view accessPointSSID, std::string_view hostname, const std::vector<WiFiCredentials>& credentialsList);
    WiFiConfig(std::string_view accessPointSSID, std::string_view accessPointPassword, std::string_view hostname, const std::vector<WiFiCredentials>& credentialsList);

    // The captive portal's network name. The default (CONFIG_OPENSHOCK_FW_AP_PREFIX) gets the hub's MAC appended, so
    // hubs side by side stay apart; any other name is used as it is.
    std::string accessPointSSID;
    // Empty: an open network. Otherwise its WPA2/WPA3 passphrase, 8-63 printable ASCII characters.
    // Defaults to CONFIG_OPENSHOCK_FW_AP_PASSWORD, which a factory reset restores.
    std::string accessPointPassword;
    std::string hostname;
    std::vector<OpenShock::Config::WiFiCredentials> credentialsList;  // in id order once loaded from NVS

    /// @brief The section runs it on every value it takes in: a hostname esp_netif would refuse (empty, over 32
    ///        characters, or not letters, digits and hyphens) becomes the default, as does an AP SSID that is empty or
    ///        over 32 bytes; so does an AP password WPA2 can't use; and a credential with no id (0) or a
    ///        repeated one gets the lowest free id (or is dropped when none is left), whichever way it came in.
    void Normalize();

    bool LoadNvs(const char* partition);
    bool SaveNvs(const char* partition, const WiFiConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  [[nodiscard]] WiFiConfig GetWiFiConfig();
  bool SetWiFiConfig(const WiFiConfig& config);

  [[nodiscard]] std::vector<WiFiCredentials> GetWiFiCredentials();
  /// @brief Calls `visit` with each saved SSID, in place: no copy of the list, and no password leaves the config.
  ///        Runs under the config's read lock, so `visit` must not call back into Config.
  void ForEachWiFiSsid(Util::FunctionRef<void(std::string_view ssid)> visit);
  // Emits each stored credential as an object into an already-open JSON array
  // (the caller opens/closes it with json_gen_{start,end}_array).
  void GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData);
  [[nodiscard]] bool HasWiFiCredentials();
  /// @brief Replaces every saved network. An id of 0 gets the lowest free id; a repeated id or a bad SSID/password
  ///        length is refused.
  bool SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials);

  /// @return The id of the stored credential (new, or the existing one for that SSID updated), or 0 on failure.
  uint8_t AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode = WIFI_AUTH_MAX);
  [[nodiscard]] std::optional<WiFiCredentials> TryGetWiFiCredentialsByID(uint8_t id);
  /// @return The id of the credentials saved for this SSID, or 0 if there are none. Copies nothing.
  uint8_t GetWiFiCredentialsIDbySSID(std::string_view ssid);
  bool PinWiFiCredentialsBSSID(uint8_t id, const uint8_t (&bssid)[6]);
  bool RemoveWiFiCredentials(uint8_t id);
  bool ClearWiFiCredentials();
  // The access point settings take effect the next time the captive portal starts.
  [[nodiscard]] std::string GetWiFiAccessPointSSID();
  /// @brief Refuses an SSID AccessPoint::IsValidSSID rejects.
  bool SetWiFiAccessPointSSID(std::string ssid);
  /// @brief Back to the default name (CONFIG_OPENSHOCK_FW_AP_PREFIX, broadcast with the hub's MAC appended).
  bool ResetWiFiAccessPointSSID();
  [[nodiscard]] std::string GetWiFiAccessPointPassword();
  /// @brief Refuses a password AccessPoint::IsValidPassword rejects. Empty makes the access point open.
  bool SetWiFiAccessPointPassword(std::string password);
  /// @brief Back to the build's default password (CONFIG_OPENSHOCK_FW_AP_PASSWORD), as a factory reset does.
  bool ResetWiFiAccessPointPassword();
  [[nodiscard]] std::string GetWiFiHostname();
  bool SetWiFiHostname(std::string hostname);
}  // namespace OpenShock::Config
