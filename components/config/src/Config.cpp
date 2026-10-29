#include <freertos/FreeRTOS.h>

#include "config/Config.h"

const char* const TAG = "Config";

#include "Chipset.h"
#include "config/RootConfig.h"
#include "Logging.h"
#include "OpenShock.h"
#include "serialization/VerifiedRoot.h"
#include "SimpleMutex.h"

#include "json/Json.h"

#include "fs/ConfigFs.h"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace OpenShock;

// littlefs partition holding the persisted config blob. The Arduino LittleFSFS
// stored it as the file "config" at the root of the partition labelled "config"
// (VFS "/config/config" resolved to lfs path "/config"); the raw-lfs ConfigFs opens
// that same path so existing devices keep loading their config after the migration.
static constexpr const char* CONFIG_PARTITION_LABEL = "config";
static constexpr const char* CONFIG_FILE_PATH       = "/config";

static OpenShock::ConfigFs _configFs;
// Constructed at static-init time, before app_main has run Board::Init(), so the RF and E-Stop constructors see
// the no-pin fallback descriptor here. That is harmless only because Init() always re-seeds it: a stored config
// goes through FromFlatbuffers, which falls back to the board default per field, and a missing or corrupt one
// through ToDefault(). Nothing may read _configData before Config::Init().
static Config::RootConfig _configData;

// Two priority-inheriting mutexes, so no reader ever waits on flash:
// - _configDataMutex guards _configData and is held only to copy it out (readers) or to commit a new one (writers).
// - _configWriteMutex serializes writers across copy, mutate, save and commit. Writers are the only ones that modify
//   _configData, so while holding it they may read _configData without _configDataMutex.
// Lock order: _configWriteMutex before _configDataMutex.
static SimpleMutex _configDataMutex;
static SimpleMutex _configWriteMutex;

// Both wait as long as it takes, so they can't fail; `retval` is kept so the call sites read the same.
#define CONFIG_LOCK_READ(retval)  ScopedLock lock__(&_configDataMutex)
#define CONFIG_LOCK_WRITE(retval) ScopedLock lock__(&_configWriteMutex)

// Requires the write lock. Publishes `config` as the running config.
static void commitConfig(Config::RootConfig&& config)
{
  ScopedLock lock__(&_configDataMutex);
  _configData = std::move(config);
}

static bool tryDeserializeConfig(const uint8_t* buffer, std::size_t bufferLen, OpenShock::Config::RootConfig& config)
{
  if (buffer == nullptr) {
    OS_LOGE(TAG, "Buffer is null");
    return false;
  }

  auto fbsConfig = Serialization::GetVerifiedRoot<Serialization::Configuration::HubConfig>(std::span<const uint8_t>(buffer, bufferLen), Config::MaxConfigSize);
  if (fbsConfig == nullptr) {
    OS_LOGE(TAG, "Config failed verification (%zu bytes, max %zu)", bufferLen, Config::MaxConfigSize);
    return false;
  }

  // Read config
  if (!config.FromFlatbuffers(fbsConfig)) {
    OS_LOGE(TAG, "Failed to read config file");
    return false;
  }

  return true;
}
// Requires the write lock.
static bool tryLoadConfig()
{
  std::vector<uint8_t> data;
  if (!_configFs.read(CONFIG_FILE_PATH, data)) {
    OS_LOGE(TAG, "Failed to read config file");
    return false;
  }

  OpenShock::Config::RootConfig loaded;
  if (!tryDeserializeConfig(data.data(), data.size(), loaded)) {
    return false;
  }

  commitConfig(std::move(loaded));
  return true;
}
static bool trySaveConfig(const uint8_t* data, std::size_t dataLen)
{
  if (dataLen > Config::MaxConfigSize) {
    OS_LOGE(TAG, "Config is too large to store (%zu bytes, max %zu)", dataLen, Config::MaxConfigSize);
    return false;
  }

  if (!_configFs.write(CONFIG_FILE_PATH, std::span<const uint8_t>(data, dataLen))) {
    OS_LOGE(TAG, "Failed to write config file (%zu bytes)", dataLen);
    return false;
  }

  return true;
}
static bool trySaveConfig(const OpenShock::Config::RootConfig& config)
{
  flatbuffers::FlatBufferBuilder builder;

  // Write every field, even ones equal to the schema default. Older firmware may use different defaults
  // (1.5.x reads a missing is_enabled/keepalive_enabled as false), so an omitted field changes meaning on downgrade.
  builder.ForceDefaults(true);

  auto fbsConfig = config.ToFlatbuffers(builder, true);

  Serialization::Configuration::FinishHubConfigBuffer(builder, fbsConfig);

  return trySaveConfig(builder.GetBufferPointer(), builder.GetSize());
}
// Requires the write lock. Applies `mutate` to a copy, persists the copy and only then commits it, so a failed
// write leaves the running config identical to what is on flash. `mutate` returns false to abort without saving.
// Readers keep reading the old config during the flash write.
template<typename Fn>
static bool mutateAndSave(Fn&& mutate)
{
  OpenShock::Config::RootConfig updated = _configData;
  if (!mutate(updated)) {
    return false;
  }

  if (!trySaveConfig(updated)) {
    return false;
  }

  commitConfig(std::move(updated));
  return true;
}

void Config::Init()
{
  CONFIG_LOCK_WRITE();

  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, CONFIG_PARTITION_LABEL);
  if (partition == nullptr) {
    OS_PANIC(TAG, "Unable to find config partition!");
  }

  if (!_configFs.mount(partition)) {
    OS_PANIC(TAG, "Unable to mount config LittleFS partition!");
  }

  if (tryLoadConfig()) {
    return;
  }

  OS_LOGW(TAG, "Failed to load config, writing default config");

  OpenShock::Config::RootConfig defaults;
  defaults.ToDefault();

  if (!trySaveConfig(defaults)) {
    OS_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }

  commitConfig(std::move(defaults));
}

std::string Config::GetAsJSON(bool withSensitiveData)
{
  CONFIG_LOCK_READ({});

  JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();

  _configData.ToJSON(gen, nullptr, withSensitiveData);

  return writer.finish();
}
bool Config::SaveFromJSON(std::string_view json)
{
  JSON::JsonDocument doc;
  if (!doc.parse(json)) {
    OS_LOGE(TAG, "Failed to parse JSON");
    return false;
  }

  CONFIG_LOCK_WRITE(false);

  // Parse into a copy and commit only once every section parsed and the result is
  // stored: RootConfig::FromJSON fills in section by section, so a failure part way
  // through would otherwise leave the earlier sections applied.
  OpenShock::Config::RootConfig config = _configData;
  if (!config.FromJSON(doc.root())) {
    OS_LOGE(TAG, "Failed to read JSON");
    return false;
  }

  if (!trySaveConfig(config)) {
    return false;
  }

  commitConfig(std::move(config));
  return true;
}

flatbuffers::Offset<Serialization::Configuration::HubConfig> Config::GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData)
{
  CONFIG_LOCK_READ(0);

  return _configData.ToFlatbuffers(builder, withSensitiveData);
}

bool Config::SaveFromFlatBuffer(const Serialization::Configuration::HubConfig* config)
{
  CONFIG_LOCK_WRITE(false);

  // Same all-or-nothing handling as SaveFromJSON.
  OpenShock::Config::RootConfig parsed = _configData;
  if (!parsed.FromFlatbuffers(config)) {
    OS_LOGE(TAG, "Failed to read config file");
    return false;
  }

  if (!trySaveConfig(parsed)) {
    return false;
  }

  commitConfig(std::move(parsed));
  return true;
}

bool Config::GetRaw(TinyVec<uint8_t>& buffer)
{
  CONFIG_LOCK_READ(false);

  // Serialize the live config rather than reading flash: no filesystem access under the read lock, and it is
  // what the device is actually running with.
  // ForceDefaults like trySaveConfig, so a raw dump stays valid when loaded into older firmware.
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  auto fbsConfig = _configData.ToFlatbuffers(builder, true);
  Serialization::Configuration::FinishHubConfigBuffer(builder, fbsConfig);

  buffer.resize(builder.GetSize());
  memcpy(buffer.data(), builder.GetBufferPointer(), builder.GetSize());

  return true;
}

bool Config::SetRaw(const uint8_t* buffer, std::size_t size)
{
  CONFIG_LOCK_WRITE(false);

  OpenShock::Config::RootConfig config;
  if (!tryDeserializeConfig(buffer, size, config)) {
    OS_LOGE(TAG, "Failed to deserialize config");
    return false;
  }

  if (!trySaveConfig(buffer, size)) {
    return false;
  }

  // Keep memory in sync with flash, or the next setter would write the old config back over the import.
  commitConfig(std::move(config));
  return true;
}

void Config::FactoryReset()
{
  CONFIG_LOCK_WRITE();

  OpenShock::Config::RootConfig defaults;
  defaults.ToDefault();

  if (!_configFs.remove(CONFIG_FILE_PATH) && _configFs.exists(CONFIG_FILE_PATH)) {
    OS_PANIC(TAG, "Failed to remove existing config file for factory reset. Reccomend formatting microcontroller and re-flashing firmware");
  }

  if (!trySaveConfig(defaults)) {
    OS_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }

  // Published only once persisted, like every other write
  commitConfig(std::move(defaults));

  OS_LOGI(TAG, "Factory reset complete");
}

std::optional<Config::RFConfig> Config::GetRFConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.rf;
}

std::optional<Config::WiFiConfig> Config::GetWiFiConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.wifi;
}

std::optional<Config::CaptivePortalConfig> Config::GetCaptivePortalConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.captivePortal;
}

std::optional<Config::BackendConfig> Config::GetBackendConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.backend;
}

std::optional<Config::SerialInputConfig> Config::GetSerialInputConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.serialInput;
}

std::optional<Config::OtaUpdateConfig> Config::GetOtaUpdateConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.otaUpdate;
}

std::optional<Config::EStopConfig> Config::GetEStopConfig()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.estop;
}

bool Config::SetRFConfig(const Config::RFConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.rf = config;
    return true;
  });
}

bool Config::SetWiFiConfig(const Config::WiFiConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.wifi = config;
    return true;
  });
}

bool Config::SetCaptivePortalConfig(const Config::CaptivePortalConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.captivePortal = config;
    return true;
  });
}

bool Config::SetBackendConfig(const Config::BackendConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.backend = config;
    return true;
  });
}

bool Config::SetSerialInputConfig(const Config::SerialInputConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.serialInput = config;
    return true;
  });
}

bool Config::SetOtaUpdateConfig(const Config::OtaUpdateConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.otaUpdate = config;
    return true;
  });
}

bool Config::UpdateOtaUpdateConfig(Util::FunctionRef<bool(Config::OtaUpdateConfig&)> update)
{
  CONFIG_LOCK_WRITE(false);

  Config::OtaUpdateConfig config = _configData.otaUpdate;
  if (!update(config)) {
    return true;  // Unchanged
  }

  return mutateAndSave([&](Config::RootConfig& root) {
    root.otaUpdate = std::move(config);
    return true;
  });
}

bool Config::SetEStopConfig(const Config::EStopConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.estop = config;
    root.estop.Normalize();
    return true;
  });
}

std::optional<std::vector<Config::WiFiCredentials>> Config::GetWiFiCredentials()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.wifi.credentialsList;
}

bool Config::GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData)
{
  CONFIG_LOCK_READ(false);

  for (auto& creds : _configData.wifi.credentialsList) {
    creds.ToJSON(gen, nullptr, withSensitiveData);
  }

  return true;
}

// 802.11 SSIDs are 1-32 bytes; WPA passphrases are up to 63 characters, or 64 for a raw hex PSK.
static bool isValidWiFiCredentials(std::string_view ssid, std::string_view password)
{
  if (ssid.empty() || ssid.size() > 32) {
    OS_LOGE(TAG, "Invalid SSID length %zu", ssid.size());
    return false;
  }
  if (password.size() > 64) {
    OS_LOGE(TAG, "Invalid password length %zu", password.size());
    return false;
  }
  return true;
}

bool Config::SetWiFiCredentials(const std::vector<Config::WiFiCredentials>& credentials)
{
  std::bitset<256> seenIds;
  for (const auto& creds : credentials) {
    if (creds.id == 0) {
      OS_LOGE(TAG, "Cannot set WiFi credentials: credential ID cannot be 0");
      return false;
    }
    if (seenIds[creds.id]) {
      OS_LOGE(TAG, "Cannot set WiFi credentials: duplicate credential ID %u", creds.id);
      return false;
    }
    seenIds[creds.id] = true;

    if (!isValidWiFiCredentials(creds.ssid, creds.password)) {
      return false;
    }
  }

  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.wifi.credentialsList = credentials;
    return true;
  });
}

std::optional<gpio_num_t> Config::GetRFConfigTxPin()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.rf.txPin;
}

bool Config::SetRFConfigTxPin(gpio_num_t txPin)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.rf.txPin = txPin;
    return true;
  });
}

std::optional<bool> Config::GetRFConfigKeepAliveEnabled()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.rf.keepAliveEnabled;
}

bool Config::SetRFConfigKeepAliveEnabled(bool enabled)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.rf.keepAliveEnabled = enabled;
    return true;
  });
}

bool Config::AnyWiFiCredentials(Util::FunctionRef<bool(const Config::WiFiCredentials&)> predicate)
{
  // The predicate is caller code, so run it on a copy outside the lock
  auto creds = GetWiFiCredentials();
  if (!creds) {
    return false;
  }

  return std::any_of(creds->begin(), creds->end(), predicate);
}

uint8_t Config::AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode)
{
  if (!isValidWiFiCredentials(ssid, password)) {
    return 0;
  }

  CONFIG_LOCK_WRITE(0);

  // Built on a copy and committed only once on flash, like mutateAndSave
  OpenShock::Config::RootConfig updated = _configData;
  auto& list                            = updated.wifi.credentialsList;

  uint8_t id = 0;

  std::bitset<255> bits;
  for (auto it = list.begin(); it != list.end();) {
    auto& creds = *it;

    if (std::string_view(creds.ssid) == ssid) {
      creds.password = password;
      if (authMode != WIFI_AUTH_MAX) {
        creds.authMode = authMode;
      }

      if (!trySaveConfig(updated)) {
        OS_LOGE(TAG, "Failed to persist updated WiFi credentials for SSID %.*s", static_cast<int>(ssid.size()), ssid.data());
        return 0;
      }

      id = creds.id;
      commitConfig(std::move(updated));
      return id;
    }

    if (creds.id == 0) {
      OS_LOGW(TAG, "Found WiFi credentials with ID 0, removing");
      it = list.erase(it);
      continue;
    }

    // Mark ID as used
    bits[creds.id - 1] = true;
    ++it;
  }

  // Get first available ID
  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (!bits[i]) {
      id = i + 1;
      break;
    }
  }

  if (id == 0) {
    OS_LOGE(TAG, "Failed to add WiFi credentials: no available IDs");
    return 0;
  }

  list.emplace_back(id, ssid, password, authMode);

  // Only keep credentials that made it to flash. This is also what caps the list:
  // once the config would outgrow MaxConfigSize, trySaveConfig refuses it.
  if (!trySaveConfig(updated)) {
    OS_LOGE(TAG, "Failed to persist new WiFi credentials for SSID %.*s", static_cast<int>(ssid.size()), ssid.data());
    return 0;
  }

  commitConfig(std::move(updated));
  return id;
}

std::optional<Config::WiFiCredentials> Config::TryGetWiFiCredentialsByID(uint8_t id)
{
  CONFIG_LOCK_READ(std::nullopt);

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.id == id) {
      return creds;
    }
  }

  return std::nullopt;
}

std::optional<Config::WiFiCredentials> Config::TryGetWiFiCredentialsBySSID(const char* ssid)
{
  CONFIG_LOCK_READ(std::nullopt);

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.ssid == ssid) {
      return creds;
    }
  }

  return std::nullopt;
}

uint8_t Config::GetWiFiCredentialsIDbySSID(const char* ssid)
{
  CONFIG_LOCK_READ(0);

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.ssid == ssid) {
      return creds.id;
    }
  }

  return 0;
}

bool Config::PinWiFiCredentialsBSSID(uint8_t id, const uint8_t (&bssid)[6])
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    for (auto& creds : root.wifi.credentialsList) {
      if (creds.id == id) {
        memcpy(creds.bssid.data(), bssid, 6);
        return true;
      }
    }
    return false;
  });
}

bool Config::RemoveWiFiCredentials(uint8_t id)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([id](Config::RootConfig& root) {
    auto& list = root.wifi.credentialsList;
    auto it    = std::find_if(list.begin(), list.end(), [id](const Config::WiFiCredentials& creds) { return creds.id == id; });
    if (it == list.end()) {
      return false;
    }
    list.erase(it);
    return true;
  });
}

bool Config::ClearWiFiCredentials()
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([](Config::RootConfig& root) {
    root.wifi.credentialsList.clear();
    return true;
  });
}

std::optional<std::string> Config::GetWiFiHostname()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.wifi.hostname;
}

bool Config::SetWiFiHostname(std::string hostname)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.wifi.hostname = std::move(hostname);
    return true;
  });
}

std::optional<std::string> Config::GetBackendDomain()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.backend.domain;
}

bool Config::SetBackendDomain(std::string domain)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.backend.domain = std::move(domain);
    return true;
  });
}

bool Config::HasBackendAuthToken()
{
  CONFIG_LOCK_READ(false);

  return !_configData.backend.authToken.empty();
}

std::optional<std::string> Config::GetBackendAuthToken()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.backend.authToken;
}

bool Config::SetBackendAuthToken(std::string token)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.backend.authToken = std::move(token);
    return true;
  });
}

bool Config::ClearBackendAuthToken()
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([](Config::RootConfig& root) {
    root.backend.authToken.clear();
    return true;
  });
}

std::optional<bool> Config::GetSerialInputConfigEchoEnabled()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.serialInput.echoEnabled;
}

bool Config::SetSerialInputConfigEchoEnabled(bool enabled)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.serialInput.echoEnabled = enabled;
    return true;
  });
}

std::optional<int32_t> Config::GetOtaUpdateId()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.otaUpdate.updateId;
}

bool Config::SetOtaUpdateId(int32_t updateId)
{
  CONFIG_LOCK_WRITE(false);

  if (_configData.otaUpdate.updateId == updateId) {
    return true;
  }

  return mutateAndSave([&](Config::RootConfig& root) {
    root.otaUpdate.updateId = updateId;
    return true;
  });
}

std::optional<OtaUpdateStep> Config::GetOtaUpdateStep()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.otaUpdate.updateStep;
}

bool Config::CompareExchangeOtaUpdateStep(OtaUpdateStep expected, OtaUpdateStep desired)
{
  CONFIG_LOCK_WRITE(false);

  // Writers are serialized by the write lock, so the step can't change between this check and the save
  if (_configData.otaUpdate.updateStep != expected) {
    return false;
  }
  if (expected == desired) {
    return true;
  }

  return mutateAndSave([&](Config::RootConfig& root) {
    root.otaUpdate.updateStep = desired;
    return true;
  });
}

bool Config::SetOtaUpdateStep(OtaUpdateStep updateStep)
{
  CONFIG_LOCK_WRITE(false);

  if (_configData.otaUpdate.updateStep == updateStep) {
    return true;
  }

  return mutateAndSave([&](Config::RootConfig& root) {
    root.otaUpdate.updateStep = updateStep;
    return true;
  });
}

std::optional<bool> Config::GetEStopEnabled()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.estop.enabled;
}

bool Config::SetEStopEnabled(bool enabled)
{
  CONFIG_LOCK_WRITE(false);

  return mutateAndSave([&](Config::RootConfig& root) {
    root.estop.enabled = enabled;
    return true;
  });
}

std::optional<gpio_num_t> Config::GetEStopGpioPin()
{
  CONFIG_LOCK_READ(std::nullopt);

  return _configData.estop.gpioPin;
}

bool Config::SetEStopGpioPin(gpio_num_t gpioPin)
{
  CONFIG_LOCK_WRITE(false);

  if (!OpenShock::IsValidInputPin(gpioPin)) {
    OS_LOGE(TAG, "Invalid EStop GPIO Pin: %d", gpioPin);
    return false;
  }

  return mutateAndSave([&](Config::RootConfig& root) {
    root.estop.gpioPin = gpioPin;
    return true;
  });
}
