#pragma once

#include "config/BackendConfig.h"
#include "config/CaptivePortalConfig.h"
#include "config/EStopConfig.h"
#include "config/OtaUpdateConfig.h"
#include "config/RFConfig.h"
#include "config/SerialInputConfig.h"
#include "config/WiFiConfig.h"
#include "config/WiFiCredentials.h"
#include "TinyVec.h"
#include "util/FunctionRef.h"

#include <hal/gpio_types.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock::Config {
  /* Largest config file that can be stored and loaded back. The "config" partition (partitions/ota_4mb.csv) is 12 KiB of littlefs with
   * 4 KiB blocks; two blocks hold the metadata pair, leaving a single block for file data. Saves of a larger config are refused, and a
   * larger file or rawconfig buffer is rejected without being loaded. */
  inline constexpr std::size_t MaxConfigSize = 4096;

  void Init();

  /* GetAsJSON and SaveFromJSON are used for Reading/Writing the config file in its human-readable form. */
  std::string GetAsJSON(bool withSensitiveData);
  bool SaveFromJSON(std::string_view json);

  /* GetAsFlatBuffer and SaveFromFlatBuffer are used for Reading/Writing the config file in its binary form. */
  [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::HubConfig> GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData);
  bool SaveFromFlatBuffer(const Serialization::Configuration::HubConfig* config);

  /* GetRaw and SetRaw are used for Reading/Writing the config file in its binary form. */
  bool GetRaw(TinyVec<uint8_t>& buffer);
  bool SetRaw(const uint8_t* buffer, std::size_t size);

  /**
   * @brief Resets the config file to the factory default values.
   *
   * @note A restart after calling this function is HIGHLY recommended.
   */
  void FactoryReset();

  [[nodiscard]] std::optional<RFConfig> GetRFConfig();
  [[nodiscard]] std::optional<WiFiConfig> GetWiFiConfig();
  [[nodiscard]] std::optional<CaptivePortalConfig> GetCaptivePortalConfig();
  [[nodiscard]] std::optional<BackendConfig> GetBackendConfig();
  [[nodiscard]] std::optional<SerialInputConfig> GetSerialInputConfig();
  [[nodiscard]] std::optional<OtaUpdateConfig> GetOtaUpdateConfig();
  [[nodiscard]] std::optional<EStopConfig> GetEStopConfig();

  bool SetRFConfig(const RFConfig& config);
  bool SetWiFiConfig(const WiFiConfig& config);
  bool SetCaptivePortalConfig(const CaptivePortalConfig& config);
  bool SetBackendConfig(const BackendConfig& config);
  bool SetSerialInputConfig(const SerialInputConfig& config);
  bool SetOtaUpdateConfig(const OtaUpdateConfig& config);
  /// @brief Read-modify-write that no other writer can interleave with. `update` edits the current config in place and
  ///        returns whether it changed anything (nothing is saved otherwise). It runs under the config write lock: keep
  ///        it to plain field updates.
  bool UpdateOtaUpdateConfig(Util::FunctionRef<bool(OtaUpdateConfig&)> update);
  bool SetEStopConfig(const EStopConfig& config);

  [[nodiscard]] std::optional<std::vector<WiFiCredentials>> GetWiFiCredentials();
  // Emits each stored credential as an object into an already-open JSON array
  // (the caller opens/closes it with json_gen_{start,end}_array).
  bool GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData);
  bool SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials);

  [[nodiscard]] std::optional<gpio_num_t> GetRFConfigTxPin();
  bool SetRFConfigTxPin(gpio_num_t txPin);
  [[nodiscard]] std::optional<bool> GetRFConfigKeepAliveEnabled();
  bool SetRFConfigKeepAliveEnabled(bool enabled);

  bool AnyWiFiCredentials(Util::FunctionRef<bool(const Config::WiFiCredentials&)> predicate);
  uint8_t AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode = WIFI_AUTH_MAX);
  [[nodiscard]] std::optional<WiFiCredentials> TryGetWiFiCredentialsByID(uint8_t id);
  [[nodiscard]] std::optional<WiFiCredentials> TryGetWiFiCredentialsBySSID(const char* ssid);
  uint8_t GetWiFiCredentialsIDbySSID(const char* ssid);
  bool PinWiFiCredentialsBSSID(uint8_t id, const uint8_t (&bssid)[6]);
  bool RemoveWiFiCredentials(uint8_t id);
  bool ClearWiFiCredentials();
  [[nodiscard]] std::optional<std::string> GetWiFiHostname();
  bool SetWiFiHostname(std::string hostname);

  [[nodiscard]] std::optional<std::string> GetBackendDomain();
  bool SetBackendDomain(std::string domain);
  bool HasBackendAuthToken();
  [[nodiscard]] std::optional<std::string> GetBackendAuthToken();
  bool SetBackendAuthToken(std::string token);
  bool ClearBackendAuthToken();

  [[nodiscard]] std::optional<bool> GetSerialInputConfigEchoEnabled();
  bool SetSerialInputConfigEchoEnabled(bool enabled);

  [[nodiscard]] std::optional<int32_t> GetOtaUpdateId();
  bool SetOtaUpdateId(int32_t updateId);
  [[nodiscard]] std::optional<OtaUpdateStep> GetOtaUpdateStep();
  bool SetOtaUpdateStep(OtaUpdateStep updateStep);
  /// @brief Sets the step only if it is still `expected`, so a stale writer can't clobber an update started meanwhile.
  /// @return True if the step was `expected` and is now `desired`
  bool CompareExchangeOtaUpdateStep(OtaUpdateStep expected, OtaUpdateStep desired);

  [[nodiscard]] std::optional<bool> GetEStopEnabled();
  bool SetEStopEnabled(bool enabled);
  [[nodiscard]] std::optional<gpio_num_t> GetEStopGpioPin();
  bool SetEStopGpioPin(gpio_num_t gpioPin);
}  // namespace OpenShock::Config
