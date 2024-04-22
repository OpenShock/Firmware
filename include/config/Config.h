#pragma once

#include "config/BackendConfig.h"
#include "config/CaptivePortalConfig.h"
#include "config/OtaUpdateConfig.h"
#include "config/RFConfig.h"
#include "config/SerialInputConfig.h"
#include "config/WiFiConfig.h"
#include "config/WiFiCredentials.h"
#include "StringView.h"

#include <functional>
#include <vector>

namespace OpenShock::Config {
  void Init();

  /* GetAsJSON and SaveFromJSON are used for Reading/Writing the config file in its human-readable form. */
  std::string GetAsJSON(bool withSensitiveData);
  bool SaveFromJSON(StringView json);

  /* GetAsFlatBuffer and SaveFromFlatBuffer are used for Reading/Writing the config file in its binary form. */
  flatbuffers::Offset<Serialization::Configuration::Config> GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData);
  bool SaveFromFlatBuffer(const Serialization::Configuration::Config* config);

  /* GetRaw and SetRaw are used for Reading/Writing the config file in its binary form. */
  bool GetRaw(std::vector<std::uint8_t>& buffer);
  bool SetRaw(const std::uint8_t* buffer, std::size_t size);

  /**
   * @brief Resets the config file to the factory default values.
   *
   * @note A restart after calling this function is HIGHLY recommended.
   */
  void FactoryReset();

  bool GetRFConfig(RFConfig& out);
  bool GetWiFiConfig(WiFiConfig& out);
  bool GetOtaUpdateConfig(OtaUpdateConfig& out);
  bool GetWiFiCredentials(cJSON* array, bool withSensitiveData);
  bool GetWiFiCredentials(std::vector<WiFiCredentials>& out);

  bool SetRFConfig(const RFConfig& config);
  bool SetWiFiConfig(const WiFiConfig& config);
  bool SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials);
  bool SetCaptivePortalConfig(const CaptivePortalConfig& config);
  bool SetSerialInputConfig(const SerialInputConfig& config);
  bool SetBackendConfig(const BackendConfig& config);

  bool GetRFConfigTxPin(std::uint8_t& out);
  bool SetRFConfigTxPin(std::uint8_t txPin);
  bool GetRFConfigKeepAliveEnabled(bool& out);
  bool SetRFConfigKeepAliveEnabled(bool enabled);

  bool GetSerialInputConfigEchoEnabled(bool& out);
  bool SetSerialInputConfigEchoEnabled(bool enabled);

  bool AnyWiFiCredentials(std::function<bool(const Config::WiFiCredentials&)> predicate);

  std::uint8_t AddWiFiCredentials(StringView ssid, StringView password);
  bool TryGetWiFiCredentialsByID(std::uint8_t id, WiFiCredentials& out);
  bool TryGetWiFiCredentialsBySSID(const char* ssid, WiFiCredentials& out);
  std::uint8_t GetWiFiCredentialsIDbySSID(const char* ssid);
  bool RemoveWiFiCredentials(std::uint8_t id);
  bool ClearWiFiCredentials();

  bool GetOtaUpdateId(std::int32_t& out);
  bool SetOtaUpdateId(std::int32_t updateId);
  bool GetOtaUpdateStep(OtaUpdateStep& out);
  bool SetOtaUpdateStep(OtaUpdateStep updateStep);

  bool GetBackendDomain(std::string& out);
  bool SetBackendDomain(StringView domain);
  bool HasBackendAuthToken();
  bool GetBackendAuthToken(std::string& out);
  bool SetBackendAuthToken(StringView token);
  bool ClearBackendAuthToken();
  bool HasBackendLCGOverride();
  bool GetBackendLCGOverride(std::string& out);
  bool SetBackendLCGOverride(StringView lcgOverride);
  bool ClearBackendLCGOverride();
}  // namespace OpenShock::Config
