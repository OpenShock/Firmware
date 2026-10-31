#pragma once

// The `wifi` config section: the soft-AP SSID, the hostname and the saved networks. Owned by core (WiFiManager) and
// kept in NVS through the config framework (config/Section.h):
//   namespace "wifi.cfg": ap_ssid (str), hostname (str)
//   namespace "wifi":     one blob per credential, key = its id in decimal ("1".."255"); enumerated, never counted

#include "config/Section.h"
#include "serialization/_fbs/HubConfig_generated.h"
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

    std::string accessPointSSID;
    std::string hostname;
    std::vector<OpenShock::Config::WiFiCredentials> credentialsList;  // in id order once loaded from NVS

    /// @brief The section runs it on every value it takes in: a hostname esp_netif would refuse (empty, over 32
    ///        characters, or not letters, digits and hyphens) becomes the default, and a credential with no id (0) or
    ///        a repeated one gets the lowest free id (or is dropped when none is left), whichever way it came in.
    void Normalize();

    bool LoadNvs(const char* partition);
    bool SaveNvs(const char* partition, const WiFiConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::WiFiConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;
  };

  [[nodiscard]] WiFiConfig GetWiFiConfig();
  bool SetWiFiConfig(const WiFiConfig& config);

  [[nodiscard]] std::vector<WiFiCredentials> GetWiFiCredentials();
  // Emits each stored credential as an object into an already-open JSON array
  // (the caller opens/closes it with json_gen_{start,end}_array).
  void GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData);
  [[nodiscard]] bool HasWiFiCredentials();
  /// @brief Replaces every saved network. An id of 0 gets the lowest free id; a repeated id or a bad SSID/password
  ///        length is refused.
  bool SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials);

  bool AnyWiFiCredentials(Util::FunctionRef<bool(const Config::WiFiCredentials&)> predicate);
  /// @return The id of the stored credential (new, or the existing one for that SSID updated), or 0 on failure.
  uint8_t AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode = WIFI_AUTH_MAX);
  [[nodiscard]] std::optional<WiFiCredentials> TryGetWiFiCredentialsByID(uint8_t id);
  [[nodiscard]] std::optional<WiFiCredentials> TryGetWiFiCredentialsBySSID(const char* ssid);
  uint8_t GetWiFiCredentialsIDbySSID(const char* ssid);
  bool PinWiFiCredentialsBSSID(uint8_t id, const uint8_t (&bssid)[6]);
  bool RemoveWiFiCredentials(uint8_t id);
  bool ClearWiFiCredentials();
  [[nodiscard]] std::string GetWiFiHostname();
  bool SetWiFiHostname(std::string hostname);
}  // namespace OpenShock::Config
