#include "Config.h"

#include "Constants.h"
#include "Logging.h"
#include "Utils/HexUtils.h"

#include <LittleFS.h>

const char* const TAG = "Config";

using namespace OpenShock;

struct MainConfig {
  Config::RFConfig rf;
  Config::WiFiConfig wifi;
  Config::CaptivePortalConfig captivePortal;
  Config::BackendConfig backend;
};

static MainConfig _mainConfig;

bool ReadFbsConfig(const Serialization::Configuration::RFConfig* fbsConfig) {
  if (fbsConfig == nullptr) {
    ESP_LOGE(TAG, "Config::RF is null");
    return false;
  }

  _mainConfig.rf = {
    .txPin = fbsConfig->tx_pin(),
  };

  return true;
}
bool ReadFbsConfig(const Serialization::Configuration::WiFiConfig* cfg) {
  if (cfg == nullptr) {
    ESP_LOGE(TAG, "Config::WiFi is null");
    return false;
  }

  auto fbsApSsid   = cfg->ap_ssid();
  auto fbsHostname = cfg->hostname();
  auto fbsCredsVec = cfg->credentials();

  if (fbsApSsid == nullptr || fbsHostname == nullptr || fbsCredsVec == nullptr) {
    ESP_LOGE(TAG, "Config::WiFi::apSsid, Config::WiFi::hostname or Config::WiFi::credentials is null");
    return false;
  }

  std::vector<Config::WiFiCredentials> credentials;
  credentials.reserve(fbsCredsVec->size());

  for (auto it = fbsCredsVec->begin(); it != fbsCredsVec->end(); ++it) {
    auto fbsCreds = *it;

    if (fbsCreds == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials contains null entry");
      continue;
    }

    auto id = fbsCreds->id();
    if (id == 0 || id > 32) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry %u has invalid ID (must be 1-32)", id);
      continue;
    }

    auto ssid = fbsCreds->ssid();
    if (ssid == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null SSID");
      continue;
    }

    auto bssid = fbsCreds->bssid();
    if (bssid == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null BSSID");
      continue;
    }

    auto password = fbsCreds->password();
    if (password == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null password");
      continue;
    }

    auto bssidArray = bssid->array();
    if (bssidArray == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null BSSID array");
      continue;
    }

    Config::WiFiCredentials creds {
      .id       = id,
      .ssid     = ssid->str(),
      .password = password->str(),
    };

    std::memcpy(creds.bssid, bssidArray->data(), 6);

    credentials.push_back(std::move(creds));
  }

  _mainConfig.wifi = {
    .apSsid      = cfg->ap_ssid()->str(),
    .hostname    = cfg->hostname()->str(),
    .credentials = std::move(credentials),
  };

  return true;
}
bool ReadFbsConfig(const Serialization::Configuration::CaptivePortalConfig* cfg) {
  if (cfg == nullptr) {
    ESP_LOGE(TAG, "Config::CaptivePortal is null");
    return false;
  }

  _mainConfig.captivePortal = {
    .alwaysEnabled = cfg->always_enabled(),
  };

  return true;
}
bool ReadFbsConfig(const Serialization::Configuration::BackendConfig* cfg) {
  if (cfg == nullptr) {
    ESP_LOGE(TAG, "Config::Backend is null");
    return false;
  }

  auto authToken = cfg->auth_token();

  if (authToken == nullptr) {
    ESP_LOGE(TAG, "Config::Backend::authToken is null");
    return false;
  }

  _mainConfig.backend = {
    .authToken = authToken->str(),
  };

  return true;
}
bool ReadFbsConfig(const Serialization::Configuration::Config* cfg) {
  if (cfg == nullptr) {
    ESP_LOGE(TAG, "Config is null");
    return false;
  }

  return ReadFbsConfig(cfg->rf()) && ReadFbsConfig(cfg->wifi()) && ReadFbsConfig(cfg->captive_portal()) && ReadFbsConfig(cfg->backend());
}

bool _tryLoadConfig() {
  File file = LittleFS.open("/config", "rb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open config file for reading");
    return false;
  }

  // Get file size
  std::size_t size = file.size();

  // Allocate buffer
  std::vector<std::uint8_t> buffer(size);

  // Read file
  if (file.read(buffer.data(), buffer.size()) != buffer.size()) {
    ESP_LOGE(TAG, "Failed to read config file, size mismatch");
    return false;
  }

  file.close();

  // Deserialize
  auto fbsConfig = flatbuffers::GetRoot<Serialization::Configuration::Config>(buffer.data());
  if (fbsConfig == nullptr) {
    ESP_LOGE(TAG, "Failed to get deserialization root for config file");
    return false;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = 4096,  // Should be enough
  };
  flatbuffers::Verifier verifier(buffer.data(), buffer.size(), verifierOptions);
  if (!fbsConfig->Verify(verifier)) {
    ESP_LOGE(TAG, "Failed to verify config file integrity");
    return false;
  }

  // Read config
  if (!ReadFbsConfig(fbsConfig)) {
    ESP_LOGE(TAG, "Failed to read config file");
    return false;
  }

  return true;
}
bool _trySaveConfig() {
  File file = LittleFS.open("/config", "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open config file for writing");
    return false;
  }

  auto& _rf            = _mainConfig.rf;
  auto& _wifi          = _mainConfig.wifi;
  auto& _backend       = _mainConfig.backend;
  auto& _captivePortal = _mainConfig.captivePortal;

  // Serialize
  flatbuffers::FlatBufferBuilder builder(1024);

  auto rfConfig = Serialization::Configuration::RFConfig(_rf.txPin);

  std::vector<flatbuffers::Offset<Serialization::Configuration::WiFiCredentials>> wifiCredentials;
  for (const auto& cred : _wifi.credentials) {
    auto bssid = Serialization::Configuration::BSSID(cred.bssid);

    wifiCredentials.push_back(Serialization::Configuration::CreateWiFiCredentials(builder, cred.id, builder.CreateString(cred.ssid), &bssid, builder.CreateString(cred.password)));
  }

  auto wifiConfig = Serialization::Configuration::CreateWiFiConfig(builder, builder.CreateString(_wifi.apSsid), builder.CreateString(_wifi.hostname), builder.CreateVector(wifiCredentials));

  auto backendConfig = Serialization::Configuration::CreateBackendConfig(builder, builder.CreateString(""), builder.CreateString(_backend.authToken));

  auto captivePortalConfig = Serialization::Configuration::CaptivePortalConfig(_captivePortal.alwaysEnabled);

  auto fbsConfig = Serialization::Configuration::CreateConfig(builder, &rfConfig, wifiConfig, &captivePortalConfig, backendConfig);

  builder.Finish(fbsConfig);

  // Write file
  if (file.write(builder.GetBufferPointer(), builder.GetSize()) != builder.GetSize()) {
    ESP_LOGE(TAG, "Failed to write config file");
    return false;
  }

  file.close();

  return true;
}

void Config::Init() {
  if (_tryLoadConfig()) {
    return;
  }

  ESP_LOGW(TAG, "Failed to load config, writing default config");

  _mainConfig = {
    .rf = {
#ifdef OPENSHOCK_TX_PIN
      .txPin = OPENSHOCK_TX_PIN,
#else
      .txPin = Constants::GPIO_INVALID,
#endif
    },
    .wifi = {
      .apSsid      = "",
      .hostname    = "",
      .credentials = {},
    },
    .captivePortal = {
      .alwaysEnabled = false,
    },
    .backend = {
      .authToken = "",
    },
  };

  if (!_trySaveConfig()) {
    ESP_PANIC(TAG, "Failed to save default config. Recommend formatting microcontroller and re-flashing firmware");
  }
}

void Config::FactoryReset() {
  if (!LittleFS.remove("/config") && LittleFS.exists("/config")) {
    ESP_PANIC(TAG, "Failed to remove existing config file for factory reset. Reccomend formatting microcontroller and re-flashing firmware");
  }
}

const Config::RFConfig& Config::GetRFConfig() {
  return _mainConfig.rf;
}

const Config::WiFiConfig& Config::GetWiFiConfig() {
  return _mainConfig.wifi;
}

const std::vector<Config::WiFiCredentials>& Config::GetWiFiCredentials() {
  return _mainConfig.wifi.credentials;
}

const Config::CaptivePortalConfig& Config::GetCaptivePortalConfig() {
  return _mainConfig.captivePortal;
}

const Config::BackendConfig& Config::GetBackendConfig() {
  return _mainConfig.backend;
}

bool Config::SetRFConfig(const RFConfig& config) {
  _mainConfig.rf = config;
  return _trySaveConfig();
}

bool Config::SetWiFiConfig(const WiFiConfig& config) {
  _mainConfig.wifi = config;
  return _trySaveConfig();
}

bool Config::SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials) {
  for (auto& cred : credentials) {
    if (cred.id == 0 || cred.id > 32) {
      ESP_LOGE(TAG, "Cannot set WiFi credentials: credential ID %u is invalid (must be 1-32)", cred.id);
      return false;
    }
  }

  _mainConfig.wifi.credentials = credentials;
  return _trySaveConfig();
}

bool Config::SetCaptivePortalConfig(const CaptivePortalConfig& config) {
  _mainConfig.captivePortal = config;
  return _trySaveConfig();
}

bool Config::SetBackendConfig(const BackendConfig& config) {
  _mainConfig.backend = config;
  return _trySaveConfig();
}

bool Config::SetRFConfigTxPin(std::uint8_t txPin) {
  _mainConfig.rf.txPin = txPin;
  return _trySaveConfig();
}

std::uint8_t Config::AddWiFiCredentials(const std::string& ssid, const std::uint8_t (&bssid)[6], const std::string& password) {
  std::uint8_t id = 0;

  // Bitmask representing available credential IDs (0-31)
  std::uint32_t bits = 0;
  for (auto& creds : _mainConfig.wifi.credentials) {
    if (creds.ssid == ssid) {
      creds.password = password;

      id = creds.id;

      break;
    }

    // Mark the credential ID as used
    bits |= 1u << creds.id;
  }

  if (id == 0) {
    id = 1;
    while (bits & (1u << (id - 1)) && id <= 32) {
      id++;
    }

    if (id > 32) {
      ESP_LOGE(TAG, "Cannot add WiFi credentials: too many credentials");
      return 0;
    }

    WiFiCredentials creds {
      .id       = id,
      .ssid     = ssid,
      .password = password,
    };

    memcpy(creds.bssid, bssid, 6);

    _mainConfig.wifi.credentials.push_back(creds);
  }

  _trySaveConfig();

  return id;
}

bool Config::TryGetWiFiCredentialsByID(std::uint8_t id, Config::WiFiCredentials& credentials) {
  for (auto& creds : _mainConfig.wifi.credentials) {
    if (creds.id == id) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

bool Config::TryGetWiFiCredentialsBySSID(const char* ssid, Config::WiFiCredentials& credentials) {
  for (auto& creds : _mainConfig.wifi.credentials) {
    if (creds.ssid == ssid) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

bool Config::TryGetWiFiCredentialsByBSSID(const std::uint8_t (&bssid)[6], Config::WiFiCredentials& credentials) {
  for (auto& creds : _mainConfig.wifi.credentials) {
    if (memcmp(creds.bssid, bssid, 6) == 0) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

void Config::RemoveWiFiCredentials(std::uint8_t id) {
  for (auto it = _mainConfig.wifi.credentials.begin(); it != _mainConfig.wifi.credentials.end(); ++it) {
    if (it->id == id) {
      _mainConfig.wifi.credentials.erase(it);
      _trySaveConfig();
      return;
    }
  }
}

void Config::ClearWiFiCredentials() {
  _mainConfig.wifi.credentials.clear();
  _trySaveConfig();
}

bool Config::HasBackendAuthToken() {
  return !_mainConfig.backend.authToken.empty();
}

const std::string& Config::GetBackendAuthToken() {
  return _mainConfig.backend.authToken;
}

bool Config::SetBackendAuthToken(const std::string& token) {
  _mainConfig.backend.authToken = token;
  return _trySaveConfig();
}

bool Config::ClearBackendAuthToken() {
  _mainConfig.backend.authToken = "";
  return _trySaveConfig();
}
