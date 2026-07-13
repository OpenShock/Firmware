#include <freertos/FreeRTOS.h>

#include "config/Config.h"

const char* const TAG = "Config";

#include "Chipset.h"
#include "config/RootConfig.h"
#include "Logging.h"
#include "OpenShock.h"
#include "Panic.h"
#include "ReadWriteMutex.h"

#include "json/Json.h"

#include "fs/ConfigFs.h"

#include <bitset>
#include <cstring>
#include <span>
#include <vector>

using namespace OpenShock;

// littlefs partition holding the persisted config blob. The Arduino LittleFSFS
// stored it as the file "config" at the root of the partition labelled "config"
// (VFS "/config/config" resolved to lfs path "/config"); the raw-lfs ConfigFs opens
// that same path so existing devices keep loading their config after the migration.
static constexpr const char* CONFIG_PARTITION_LABEL = "config";
static constexpr const char* CONFIG_FILE_PATH       = "/config";

static OpenShock::ConfigFs _configFs;
static Config::RootConfig _configData;
static ReadWriteMutex _configMutex;

#define CONFIG_LOCK_READ_ACTION(retval, action)  \
  ScopedReadLock lock__(&_configMutex);          \
  if (!lock__.isLocked()) {                      \
    OS_LOGE(TAG, "Failed to acquire read lock"); \
    action;                                      \
    return retval;                               \
  }

#define CONFIG_LOCK_WRITE_ACTION(retval, action)  \
  ScopedWriteLock lock__(&_configMutex);          \
  if (!lock__.isLocked()) {                       \
    OS_LOGE(TAG, "Failed to acquire write lock"); \
    action;                                       \
    return retval;                                \
  }

#define CONFIG_LOCK_READ(retval)  CONFIG_LOCK_READ_ACTION(retval, {})
#define CONFIG_LOCK_WRITE(retval) CONFIG_LOCK_WRITE_ACTION(retval, {})

static bool tryDeserializeConfig(const uint8_t* buffer, std::size_t bufferLen, OpenShock::Config::RootConfig& config)
{
  if (buffer == nullptr || bufferLen < sizeof(flatbuffers::uoffset_t)) {
    OS_LOGE(TAG, "Buffer is null or too small");
    return false;
  }

  // Validate buffer before accessing
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = 4096,  // Should be enough
  };
  flatbuffers::Verifier verifier(buffer, bufferLen, verifierOptions);
  if (!verifier.VerifyBuffer<Serialization::Configuration::HubConfig>()) {
    OS_LOGE(TAG, "Failed to verify config file integrity");
    return false;
  }

  // Deserialize (safe after verification)
  auto fbsConfig = flatbuffers::GetRoot<Serialization::Configuration::HubConfig>(buffer);

  // Read config
  if (!config.FromFlatbuffers(fbsConfig)) {
    OS_LOGE(TAG, "Failed to read config file");
    return false;
  }

  return true;
}
static bool tryLoadConfig(TinyVec<uint8_t>& buffer)
{
  std::vector<uint8_t> data;
  if (!_configFs.read(CONFIG_FILE_PATH, data)) {
    OS_LOGE(TAG, "Failed to read config file");
    return false;
  }

  buffer.resize(data.size());
  if (!data.empty()) {
    memcpy(buffer.data(), data.data(), data.size());
  }

  return true;
}
static bool tryLoadConfig()
{
  TinyVec<uint8_t> buffer;
  if (!tryLoadConfig(buffer)) {
    return false;
  }

  return tryDeserializeConfig(buffer.data(), buffer.size(), _configData);
}
static bool trySaveConfig(const uint8_t* data, std::size_t dataLen)
{
  if (!_configFs.write(CONFIG_FILE_PATH, std::span<const uint8_t>(data, dataLen))) {
    OS_LOGE(TAG, "Failed to write config file");
    return false;
  }

  return true;
}
static bool trySaveConfig()
{
  flatbuffers::FlatBufferBuilder builder;

  auto fbsConfig = _configData.ToFlatbuffers(builder, true);

  Serialization::Configuration::FinishHubConfigBuffer(builder, fbsConfig);

  return trySaveConfig(builder.GetBufferPointer(), builder.GetSize());
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

  _configData.ToDefault();

  if (!trySaveConfig()) {
    OS_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }
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

  if (!_configData.FromJSON(doc.root())) {
    OS_LOGE(TAG, "Failed to read JSON");
    return false;
  }

  return trySaveConfig();
}

flatbuffers::Offset<Serialization::Configuration::HubConfig> Config::GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData)
{
  CONFIG_LOCK_READ(0);

  return _configData.ToFlatbuffers(builder, withSensitiveData);
}

bool Config::SaveFromFlatBuffer(const Serialization::Configuration::HubConfig* config)
{
  CONFIG_LOCK_WRITE(false);

  if (!_configData.FromFlatbuffers(config)) {
    OS_LOGE(TAG, "Failed to read config file");
    return false;
  }

  return trySaveConfig();
}

bool Config::GetRaw(TinyVec<uint8_t>& buffer)
{
  CONFIG_LOCK_READ(false);

  return tryLoadConfig(buffer);
}

bool Config::SetRaw(const uint8_t* buffer, std::size_t size)
{
  CONFIG_LOCK_WRITE(false);

  OpenShock::Config::RootConfig config;
  if (!tryDeserializeConfig(buffer, size, config)) {
    OS_LOGE(TAG, "Failed to deserialize config");
    return false;
  }

  return trySaveConfig(buffer, size);
}

void Config::FactoryReset()
{
  CONFIG_LOCK_WRITE();

  _configData.ToDefault();

  if (!_configFs.remove(CONFIG_FILE_PATH) && _configFs.exists(CONFIG_FILE_PATH)) {
    OS_PANIC(TAG, "Failed to remove existing config file for factory reset. Reccomend formatting microcontroller and re-flashing firmware");
  }

  if (!trySaveConfig()) {
    OS_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }

  OS_LOGI(TAG, "Factory reset complete");
}

bool Config::GetRFConfig(Config::RFConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.rf;

  return true;
}

bool Config::GetWiFiConfig(Config::WiFiConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.wifi;

  return true;
}

bool Config::GetCaptivePortalConfig(Config::CaptivePortalConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.captivePortal;

  return true;
}

bool Config::GetBackendConfig(Config::BackendConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.backend;

  return true;
}

bool Config::GetSerialInputConfig(Config::SerialInputConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.serialInput;

  return true;
}

bool Config::GetOtaUpdateConfig(Config::OtaUpdateConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.otaUpdate;

  return true;
}

bool Config::GetEStop(Config::EStopConfig& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.estop;

  return true;
}

bool Config::SetRFConfig(const Config::RFConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.rf = config;
  return trySaveConfig();
}

bool Config::SetWiFiConfig(const Config::WiFiConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.wifi = config;
  return trySaveConfig();
}

bool Config::SetCaptivePortalConfig(const Config::CaptivePortalConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.captivePortal = config;
  return trySaveConfig();
}

bool Config::SetBackendConfig(const Config::BackendConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.backend = config;
  return trySaveConfig();
}

bool Config::SetSerialInputConfig(const Config::SerialInputConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.serialInput = config;
  return trySaveConfig();
}

bool Config::SetOtaUpdateConfig(const Config::OtaUpdateConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.otaUpdate = config;
  return trySaveConfig();
}

bool Config::SetEStop(const Config::EStopConfig& config)
{
  CONFIG_LOCK_WRITE(false);

  _configData.estop = config;
  return trySaveConfig();
}

bool Config::GetWiFiCredentials(std::vector<Config::WiFiCredentials>& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.wifi.credentialsList;

  return true;
}

bool Config::GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData)
{
  CONFIG_LOCK_READ(false);

  for (auto& creds : _configData.wifi.credentialsList) {
    creds.ToJSON(gen, nullptr, withSensitiveData);
  }

  return true;
}

bool Config::SetWiFiCredentials(const std::vector<Config::WiFiCredentials>& credentials)
{
  bool foundZeroId = std::any_of(credentials.begin(), credentials.end(), [](const Config::WiFiCredentials& creds) { return creds.id == 0; });
  if (foundZeroId) {
    OS_LOGE(TAG, "Cannot set WiFi credentials: credential ID cannot be 0");
    return false;
  }

  CONFIG_LOCK_WRITE(false);

  _configData.wifi.credentialsList = credentials;
  return trySaveConfig();
}

bool Config::GetRFConfigTxPin(gpio_num_t& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.rf.txPin;

  return true;
}

bool Config::SetRFConfigTxPin(gpio_num_t txPin)
{
  CONFIG_LOCK_WRITE(false);

  _configData.rf.txPin = txPin;
  return trySaveConfig();
}

bool Config::GetRFConfigKeepAliveEnabled(bool& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.rf.keepAliveEnabled;

  return true;
}

bool Config::SetRFConfigKeepAliveEnabled(bool enabled)
{
  CONFIG_LOCK_WRITE(false);

  _configData.rf.keepAliveEnabled = enabled;
  return trySaveConfig();
}

bool Config::AnyWiFiCredentials(std::function<bool(const Config::WiFiCredentials&)> predicate)
{
  CONFIG_LOCK_READ(false);

  auto& creds = _configData.wifi.credentialsList;

  return std::any_of(creds.begin(), creds.end(), predicate);
}

uint8_t Config::AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode)
{
  CONFIG_LOCK_WRITE(0);

  uint8_t id = 0;

  std::bitset<255> bits;
  for (auto it = _configData.wifi.credentialsList.begin(); it != _configData.wifi.credentialsList.end();) {
    auto& creds = *it;

    if (std::string_view(creds.ssid) == ssid) {
      creds.password = password;
      if (authMode != WIFI_AUTH_MAX) {
        creds.authMode = authMode;
      }

      if (!trySaveConfig()) {
        OS_LOGE(TAG, "Failed to persist updated WiFi credentials for SSID %.*s", static_cast<int>(ssid.size()), ssid.data());
        return 0;
      }
      return creds.id;
    }

    if (creds.id == 0) {
      OS_LOGW(TAG, "Found WiFi credentials with ID 0, removing");
      it = _configData.wifi.credentialsList.erase(it);
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

  _configData.wifi.credentialsList.emplace_back(id, ssid, password, authMode);
  trySaveConfig();

  return id;
}

bool Config::TryGetWiFiCredentialsByID(uint8_t id, Config::WiFiCredentials& credentials)
{
  CONFIG_LOCK_READ(false);

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.id == id) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

bool Config::TryGetWiFiCredentialsBySSID(const char* ssid, Config::WiFiCredentials& credentials)
{
  CONFIG_LOCK_READ(false);

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.ssid == ssid) {
      credentials = creds;
      return true;
    }
  }

  return false;
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

  for (auto& creds : _configData.wifi.credentialsList) {
    if (creds.id == id) {
      memcpy(creds.bssid.data(), bssid, 6);
      return trySaveConfig();
    }
  }

  return false;
}

bool Config::RemoveWiFiCredentials(uint8_t id)
{
  CONFIG_LOCK_WRITE(false);

  for (auto it = _configData.wifi.credentialsList.begin(); it != _configData.wifi.credentialsList.end(); ++it) {
    if (it->id == id) {
      _configData.wifi.credentialsList.erase(it);
      trySaveConfig();
      return true;
    }
  }

  return false;
}

bool Config::ClearWiFiCredentials()
{
  CONFIG_LOCK_WRITE(false);

  _configData.wifi.credentialsList.clear();

  return trySaveConfig();
}

bool Config::GetWiFiHostname(std::string& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.wifi.hostname;

  return true;
}

bool Config::SetWiFiHostname(std::string hostname)
{
  CONFIG_LOCK_WRITE(false);

  _configData.wifi.hostname = std::move(hostname);

  return trySaveConfig();
}

bool Config::GetBackendDomain(std::string& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.backend.domain;

  return true;
}

bool Config::SetBackendDomain(std::string domain)
{
  CONFIG_LOCK_WRITE(false);

  _configData.backend.domain = std::move(domain);
  return trySaveConfig();
}

bool Config::HasBackendAuthToken()
{
  CONFIG_LOCK_READ(false);

  return !_configData.backend.authToken.empty();
}

bool Config::GetBackendAuthToken(std::string& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.backend.authToken;

  return true;
}

bool Config::SetBackendAuthToken(std::string token)
{
  CONFIG_LOCK_WRITE(false);

  _configData.backend.authToken = std::move(token);
  return trySaveConfig();
}

bool Config::ClearBackendAuthToken()
{
  CONFIG_LOCK_WRITE(false);

  _configData.backend.authToken.clear();
  return trySaveConfig();
}

bool Config::GetSerialInputConfigEchoEnabled(bool& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.serialInput.echoEnabled;
  return true;
}

bool Config::SetSerialInputConfigEchoEnabled(bool enabled)
{
  CONFIG_LOCK_WRITE(false);

  _configData.serialInput.echoEnabled = enabled;
  return trySaveConfig();
}

bool Config::GetOtaUpdateId(int32_t& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.otaUpdate.updateId;

  return true;
}

bool Config::SetOtaUpdateId(int32_t updateId)
{
  CONFIG_LOCK_WRITE(false);

  if (_configData.otaUpdate.updateId == updateId) {
    return true;
  }

  _configData.otaUpdate.updateId = updateId;
  return trySaveConfig();
}

bool Config::GetOtaUpdateStep(OtaUpdateStep& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.otaUpdate.updateStep;

  return true;
}

bool Config::SetOtaUpdateStep(OtaUpdateStep updateStep)
{
  CONFIG_LOCK_WRITE(false);

  if (_configData.otaUpdate.updateStep == updateStep) {
    return true;
  }

  _configData.otaUpdate.updateStep = updateStep;
  return trySaveConfig();
}

bool Config::GetEStopEnabled(bool& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.estop.enabled;

  return true;
}

bool Config::SetEStopEnabled(bool enabled)
{
  CONFIG_LOCK_WRITE(false);

  _configData.estop.enabled = enabled;
  return trySaveConfig();
}

bool Config::GetEStopGpioPin(gpio_num_t& out)
{
  CONFIG_LOCK_READ(false);

  out = _configData.estop.gpioPin;

  return true;
}

bool Config::SetEStopGpioPin(gpio_num_t gpioPin)
{
  CONFIG_LOCK_WRITE(false);

  if (!OpenShock::IsValidInputPin(gpioPin)) {
    OS_LOGE(TAG, "Invalid EStop GPIO Pin: %d", gpioPin);
    return false;
  }

  _configData.estop.gpioPin = gpioPin;
  return trySaveConfig();
}
