#pragma once

#include "serialization/_fbs/ConfigFile_generated.h"

#include <functional>
#include <string>
#include <vector>

namespace OpenShock::Config {
  // This is a copy of the flatbuffers schema defined in schemas/ConfigFile.fbs
  struct RFConfig {
    std::uint8_t txPin;
    bool keepAliveEnabled;
  };
  struct WiFiCredentials {
    std::uint8_t id;
    std::string ssid;
    std::uint8_t bssid[6];
    std::string password;
  };
  struct WiFiConfig {
    std::string apSsid;
    std::string hostname;
    std::vector<WiFiCredentials> credentials;
  };
  struct CaptivePortalConfig {
    bool alwaysEnabled;
  };
  struct BackendConfig {
    std::string authToken;
  };

  void Init();

  /**
   * @brief Resets the config file to the factory default values.
   *
   * @note A reboot after calling this function is HIGHLY recommended.
   */
  void FactoryReset();

  const RFConfig& GetRFConfig();
  const WiFiConfig& GetWiFiConfig();
  const std::vector<WiFiCredentials>& GetWiFiCredentials();
  const CaptivePortalConfig& GetCaptivePortalConfig();
  const BackendConfig& GetBackendConfig();

  bool SetRFConfig(const RFConfig& config);
  bool SetWiFiConfig(const WiFiConfig& config);
  bool SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials);
  bool SetCaptivePortalConfig(const CaptivePortalConfig& config);
  bool SetBackendConfig(const BackendConfig& config);

  bool SetRFConfigTxPin(std::uint8_t txPin);
  bool SetRFConfigKeepAliveEnabled(bool enabled);

  std::uint8_t AddWiFiCredentials(const std::string& ssid, const std::uint8_t (&bssid)[6], const std::string& password);
  bool TryGetWiFiCredentialsByID(std::uint8_t id, WiFiCredentials& out);
  bool TryGetWiFiCredentialsBySSID(const char* ssid, WiFiCredentials& out);
  bool TryGetWiFiCredentialsByBSSID(const std::uint8_t (&bssid)[6], WiFiCredentials& out);
  std::uint8_t GetWiFiCredentialsIDbySSID(const char* ssid);
  std::uint8_t GetWiFiCredentialsIDbyBSSID(const std::uint8_t (&bssid)[6]);
  std::uint8_t GetWiFiCredentialsIDbyBSSIDorSSID(const std::uint8_t (&bssid)[6], const char* ssid);
  bool RemoveWiFiCredentials(std::uint8_t id);
  void ClearWiFiCredentials();

  bool HasBackendAuthToken();
  const std::string& GetBackendAuthToken();
  bool SetBackendAuthToken(const std::string& token);
  bool ClearBackendAuthToken();
}  // namespace OpenShock::Config
