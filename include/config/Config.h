#pragma once

#include "config/BackendConfig.h"
#include "config/CaptivePortalConfig.h"
#include "config/RFConfig.h"
#include "config/WiFiConfig.h"
#include "config/WiFiCredentials.h"

#include <functional>
#include <string>
#include <vector>

namespace OpenShock::Config {
  void Init();

  /* Get the config file translated to JSON. */
  std::string GetAsJSON();

  /* Save the config file from JSON. */
  bool SaveFromJSON(const std::string& json);

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

  bool SaveChanges();
}  // namespace OpenShock::Config
