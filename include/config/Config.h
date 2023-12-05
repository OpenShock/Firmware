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

  /* GetAsJSON and SaveFromJSON are used for Reading/Writing the config file in its human-readable form. */
  std::string GetAsJSON();
  bool SaveFromJSON(const std::string& json);

  /* GetRaw and SetRaw are used for Reading/Writing the config file in its binary form. */
  bool GetRaw(std::vector<std::uint8_t>& buffer);
  bool SetRaw(const std::uint8_t* buffer, std::size_t size);

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

  std::uint8_t AddWiFiCredentials(const std::string& ssid, const std::string& password);
  bool TryGetWiFiCredentialsByID(std::uint8_t id, WiFiCredentials& out);
  bool TryGetWiFiCredentialsBySSID(const char* ssid, WiFiCredentials& out);
  std::uint8_t GetWiFiCredentialsIDbySSID(const char* ssid);
  bool RemoveWiFiCredentials(std::uint8_t id);
  void ClearWiFiCredentials();

  bool HasBackendAuthToken();
  const std::string& GetBackendAuthToken();
  bool SetBackendAuthToken(const std::string& token);
  bool ClearBackendAuthToken();
}  // namespace OpenShock::Config
