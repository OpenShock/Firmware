#pragma once

#include "fbs/ConfigFile_generated.h"

#include <functional>
#include <string>
#include <vector>

namespace OpenShock::Config {
  // This is a copy of the flatbuffers schema defined in schemas/ConfigFile.fbs
  struct RFConfig {
    std::uint32_t txPin;
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

  const RFConfig& GetRFConfig();
  const WiFiConfig& GetWiFiConfig();
  const std::vector<WiFiCredentials>& GetWiFiCredentials();
  const CaptivePortalConfig& GetCaptivePortalConfig();
  const BackendConfig& GetBackendConfig();

  void SetRFConfig(const RFConfig& config);
  void SetWiFiConfig(const WiFiConfig& config);
  void SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials);
  void SetCaptivePortalConfig(const CaptivePortalConfig& config);
  void SetBackendConfig(const BackendConfig& config);

  std::uint8_t AddWiFiCredentials(const std::string& ssid, std::uint8_t (&bssid)[6], const std::string& password);
  bool TryGetWiFiCredentialsById(std::uint8_t id, WiFiCredentials& out);
  void RemoveWiFiCredentials(std::uint8_t id);
  void ClearWiFiCredentials();

  bool HasBackendAuthToken();
  const std::string& GetBackendAuthToken();
  void SetBackendAuthToken(const std::string& token);
  void ClearBackendAuthToken();
}  // namespace OpenShock::Config
