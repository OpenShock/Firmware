#include "config/Config.h"

#include "Common.h"
#include "config/RootConfig.h"
#include "Logging.h"
#include "ReadWriteMutex.h"

#include <FS.h>
#include <LittleFS.h>

#include <cJSON.h>

#include <bitset>

const char* const TAG = "Config";

using namespace OpenShock;

static fs::LittleFSFS _configFS;
static Config::RootConfig _configData;
static ReadWriteMutex _configMutex;

bool _tryDeserializeConfig(const std::uint8_t* buffer, std::size_t bufferLen, OpenShock::Config::RootConfig& config) {
  if (buffer == nullptr || bufferLen == 0) {
    ESP_LOGE(TAG, "Buffer is null or empty");
    return false;
  }

  // Deserialize
  auto fbsConfig = flatbuffers::GetRoot<Serialization::Configuration::Config>(buffer);
  if (fbsConfig == nullptr) {
    ESP_LOGE(TAG, "Failed to get deserialization root for config file");
    return false;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = 4096,  // Should be enough
  };
  flatbuffers::Verifier verifier(buffer, bufferLen, verifierOptions);
  if (!fbsConfig->Verify(verifier)) {
    ESP_LOGE(TAG, "Failed to verify config file integrity");
    return false;
  }

  // Read config
  if (!config.FromFlatbuffers(fbsConfig)) {
    ESP_LOGE(TAG, "Failed to read config file");
    return false;
  }

  return true;
}
bool _tryLoadConfig(std::vector<std::uint8_t>& buffer) {
  File file = _configFS.open("/config", "rb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open config file for reading");
    return false;
  }

  // Get file size
  std::size_t size = file.size();

  // Resize buffer
  buffer.resize(size);

  // Read file
  if (file.read(buffer.data(), buffer.size()) != buffer.size()) {
    ESP_LOGE(TAG, "Failed to read config file, size mismatch");
    return false;
  }

  file.close();

  return true;
}
bool _tryLoadConfig() {
  std::vector<std::uint8_t> buffer;
  if (!_tryLoadConfig(buffer)) {
    return false;
  }

  return _tryDeserializeConfig(buffer.data(), buffer.size(), _configData);
}
bool _trySaveConfig(const std::uint8_t* data, std::size_t dataLen) {
  File file = _configFS.open("/config", "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open config file for writing");
    return false;
  }

  // Write file
  if (file.write(data, dataLen) != dataLen) {
    ESP_LOGE(TAG, "Failed to write config file");
    return false;
  }

  file.close();

  return true;
}
bool _trySaveConfig() {
  flatbuffers::FlatBufferBuilder builder;

  auto fbsConfig = _configData.ToFlatbuffers(builder, true);

  builder.Finish(fbsConfig);

  return _trySaveConfig(builder.GetBufferPointer(), builder.GetSize());
}

void Config::Init() {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    return;
  }

  if (!_configFS.begin(true, "/config", 3, "config")) {
    ESP_PANIC(TAG, "Unable to mount config LittleFS partition!");
  }

  if (_tryLoadConfig()) {
    return;
  }

  ESP_LOGW(TAG, "Failed to load config, writing default config");

  _configData.ToDefault();

  if (!_trySaveConfig()) {
    ESP_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }
}

std::string Config::GetAsJSON(bool withSensitiveData) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    return "";
  }

  cJSON* root = _configData.ToJSON(withSensitiveData);

  lock.unlock();

  char* json = cJSON_PrintUnformatted(root);

  std::string result(json);

  free(json);

  cJSON_Delete(root);

  return result;
}
bool Config::SaveFromJSON(StringView json) {
  cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
  if (root == nullptr) {
    ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
    return false;
  }

  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    cJSON_Delete(root);
    return false;
  }

  bool result = _configData.FromJSON(root);

  cJSON_Delete(root);

  if (!result) {
    ESP_LOGE(TAG, "Failed to read JSON");
    return false;
  }

  return _trySaveConfig();
}

flatbuffers::Offset<Serialization::Configuration::Config> Config::GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    return 0;
  }

  return _configData.ToFlatbuffers(builder, withSensitiveData);
}

bool Config::SaveFromFlatBuffer(const Serialization::Configuration::Config* config) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  if (!_configData.FromFlatbuffers(config)) {
    ESP_LOGE(TAG, "Failed to read config file");
    return false;
  }

  return _trySaveConfig();
}

bool Config::GetRaw(std::vector<std::uint8_t>& buffer) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  return _tryLoadConfig(buffer);
}

bool Config::SetRaw(const std::uint8_t* buffer, std::size_t size) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  OpenShock::Config::RootConfig config;
  if (!_tryDeserializeConfig(buffer, size, config)) {
    ESP_LOGE(TAG, "Failed to deserialize config");
    return false;
  }

  return _trySaveConfig(buffer, size);
}

void Config::FactoryReset() {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return;
  }

  _configData.ToDefault();

  if (!_configFS.remove("/config") && _configFS.exists("/config")) {
    ESP_PANIC(TAG, "Failed to remove existing config file for factory reset. Reccomend formatting microcontroller and re-flashing firmware");
  }

  if (!_trySaveConfig()) {
    ESP_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }

  ESP_LOGI(TAG, "Factory reset complete");
}

bool Config::GetRFConfig(Config::RFConfig& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.rf;

  return true;
}

bool Config::GetWiFiConfig(Config::WiFiConfig& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.wifi;

  return true;
}

bool Config::GetOtaUpdateConfig(Config::OtaUpdateConfig& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.otaUpdate;

  return true;
}

bool Config::GetWiFiCredentials(cJSON* array, bool withSensitiveData) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  for (auto& creds : _configData.wifi.credentialsList) {
    cJSON* jsonCreds = creds.ToJSON(withSensitiveData);

    cJSON_AddItemToArray(array, jsonCreds);
  }

  return true;
}

bool Config::GetWiFiCredentials(std::vector<Config::WiFiCredentials>& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.wifi.credentialsList;

  return true;
}

bool Config::SetRFConfig(const Config::RFConfig& config) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.rf = config;
  return _trySaveConfig();
}

bool Config::SetWiFiConfig(const Config::WiFiConfig& config) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.wifi = config;
  return _trySaveConfig();
}

bool Config::SetWiFiCredentials(const std::vector<Config::WiFiCredentials>& credentials) {
  bool foundZeroId = std::any_of(credentials.begin(), credentials.end(), [](const Config::WiFiCredentials& creds) { return creds.id == 0; });
  if (foundZeroId) {
    ESP_LOGE(TAG, "Cannot set WiFi credentials: credential ID cannot be 0");
    return false;
  }

  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.wifi.credentialsList = credentials;
  return _trySaveConfig();
}

bool Config::SetCaptivePortalConfig(const Config::CaptivePortalConfig& config) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.captivePortal = config;
  return _trySaveConfig();
}

bool Config::SetSerialInputConfig(const Config::SerialInputConfig& config) {
  _configData.serialInput = config;
  return _trySaveConfig();
}

bool Config::GetSerialInputConfigEchoEnabled(bool& out) {
  out = _configData.serialInput.echoEnabled;
  return true;
}

bool Config::SetSerialInputConfigEchoEnabled(bool enabled) {
  _configData.serialInput.echoEnabled = enabled;
  return _trySaveConfig();
}

bool Config::SetBackendConfig(const Config::BackendConfig& config) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.backend = config;
  return _trySaveConfig();
}

bool Config::GetRFConfigTxPin(std::uint8_t& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.rf.txPin;

  return true;
}

bool Config::SetRFConfigTxPin(std::uint8_t txPin) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.rf.txPin = txPin;
  return _trySaveConfig();
}

bool Config::GetRFConfigKeepAliveEnabled(bool& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.rf.keepAliveEnabled;

  return true;
}

bool Config::SetRFConfigKeepAliveEnabled(bool enabled) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.rf.keepAliveEnabled = enabled;
  return _trySaveConfig();
}

bool Config::AnyWiFiCredentials(std::function<bool(const Config::WiFiCredentials&)> predicate) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
  }

  auto& creds = _configData.wifi.credentialsList;

  return std::any_of(creds.begin(), creds.end(), predicate);
}

std::uint8_t Config::AddWiFiCredentials(StringView ssid, StringView password) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return 0;
  }

  std::uint8_t id = 0;

  std::bitset<255> bits;
  for (auto it = _configData.wifi.credentialsList.begin(); it != _configData.wifi.credentialsList.end(); ++it) {
    auto& creds = *it;

    if (StringView(creds.ssid) == ssid) {
      creds.password = password;

      id = creds.id;

      break;
    }

    if (creds.id == 0) {
      ESP_LOGW(TAG, "Found WiFi credentials with ID 0, removing");
      it = _configData.wifi.credentialsList.erase(it);
      continue;
    }

    // Mark ID as used
    bits[creds.id - 1] = true;
  }

  // Get first available ID
  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (!bits[i]) {
      id = i + 1;
      break;
    }
  }

  if (id == 0) {
    ESP_LOGE(TAG, "Failed to add WiFi credentials: no available IDs");
    return 0;
  }

  _configData.wifi.credentialsList.push_back({
    .id       = id,
    .ssid     = ssid.toString(),
    .password = password.toString(),
  });
  _trySaveConfig();

  return id;
}

bool Config::TryGetWiFiCredentialsByID(std::uint8_t id, Config::WiFiCredentials& credentials) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.id == id) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

bool Config::TryGetWiFiCredentialsBySSID(const char* ssid, Config::WiFiCredentials& credentials) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.ssid == ssid) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

std::uint8_t Config::GetWiFiCredentialsIDbySSID(const char* ssid) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return 0;
  }

  for (const auto& creds : _configData.wifi.credentialsList) {
    if (creds.ssid == ssid) {
      return creds.id;
    }
  }

  return 0;
}

bool Config::RemoveWiFiCredentials(std::uint8_t id) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  for (auto it = _configData.wifi.credentialsList.begin(); it != _configData.wifi.credentialsList.end(); ++it) {
    if (it->id == id) {
      _configData.wifi.credentialsList.erase(it);
      _trySaveConfig();
      return true;
    }
  }

  return false;
}

bool Config::ClearWiFiCredentials() {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.wifi.credentialsList.clear();

  return _trySaveConfig();
}

bool Config::GetOtaUpdateId(std::int32_t& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
  }

  out = _configData.otaUpdate.updateId;

  return true;
}

bool Config::SetOtaUpdateId(std::int32_t updateId) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
  }

  if (_configData.otaUpdate.updateId == updateId) {
    return true;
  }

  _configData.otaUpdate.updateId = updateId;
  return _trySaveConfig();
}

bool Config::GetOtaUpdateStep(OtaUpdateStep& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
  }

  out = _configData.otaUpdate.updateStep;

  return true;
}

bool Config::SetOtaUpdateStep(OtaUpdateStep updateStep) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
  }

  if (_configData.otaUpdate.updateStep == updateStep) {
    return true;
  }

  _configData.otaUpdate.updateStep = updateStep;
  return _trySaveConfig();
}

bool Config::GetBackendDomain(std::string& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.backend.domain;

  return true;
}

bool Config::SetBackendDomain(StringView domain) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.backend.domain = domain.toString();
  return _trySaveConfig();
}

bool Config::HasBackendAuthToken() {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  return !_configData.backend.authToken.empty();
}

bool Config::GetBackendAuthToken(std::string& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.backend.authToken;

  return true;
}

bool Config::SetBackendAuthToken(StringView token) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.backend.authToken = token.toString();
  return _trySaveConfig();
}

bool Config::ClearBackendAuthToken() {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.backend.authToken.clear();
  return _trySaveConfig();
}

bool Config::HasBackendLCGOverride() {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  return !_configData.backend.lcgOverride.empty();
}

bool Config::GetBackendLCGOverride(std::string& out) {
  ScopedReadLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire read lock");
    return false;
  }

  out = _configData.backend.lcgOverride;

  return true;
}

bool Config::SetBackendLCGOverride(StringView lcgOverride) {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.backend.lcgOverride = lcgOverride.toString();
  return _trySaveConfig();
}

bool Config::ClearBackendLCGOverride() {
  ScopedWriteLock lock(&_configMutex);
  if (!lock.isLocked()) {
    ESP_LOGE(TAG, "Failed to acquire write lock");
    return false;
  }

  _configData.backend.lcgOverride.clear();
  return _trySaveConfig();
}
