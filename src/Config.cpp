#include "Config.h"

#include "Constants.h"
#include "Logging.h"
#include "Utils/FS.h"
#include "Utils/HexUtils.h"

#include <esp_littlefs.h>

const char* const TAG        = "Config";
const char* const ConfigPath = "/config";

using namespace OpenShock;

struct MainConfig {
  Config::RFConfig rf;
  Config::WiFiConfig wifi;
  Config::CaptivePortalConfig captivePortal;
  Config::BackendConfig backend;
};

static std::shared_ptr<FileSystem> s_fileSystem;
static MainConfig s_mainConfig;

bool ReadFbsConfig(const Serialization::Configuration::RFConfig* fbsConfig) {
  if (fbsConfig == nullptr) {
    ESP_LOGE(TAG, "Config::RF is null");
    return false;
  }

  s_mainConfig.rf = {
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
      return false;
    }

    auto id = fbsCreds->id();
    if (id == 0) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has invalid ID");
      return false;
    }

    auto ssid = fbsCreds->ssid();
    if (ssid == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null SSID");
      return false;
    }

    auto bssid = fbsCreds->bssid();
    if (bssid == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null BSSID");
      return false;
    }

    auto password = fbsCreds->password();
    if (password == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null password");
      return false;
    }

    auto bssidArray = bssid->array();
    if (bssidArray == nullptr) {
      ESP_LOGE(TAG, "Config::WiFi::credentials entry has null BSSID array");
      return false;
    }

    Config::WiFiCredentials creds {
      .id       = id,
      .ssid     = ssid->str(),
      .password = password->str(),
    };

    std::memcpy(creds.bssid, bssidArray->data(), 6);

    credentials.push_back(std::move(creds));
  }

  s_mainConfig.wifi = {
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

  s_mainConfig.captivePortal = {
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

  s_mainConfig.backend = {
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
  FILE* file = fopen(ConfigPath, "rb");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open config file for reading");
    return false;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size <= 0) {
    ESP_LOGE(TAG, "Failed to read config file, size is 0");
    fclose(file);
    return false;
  }

  // Allocate buffer
  std::vector<std::uint8_t> buffer(size);

  std::size_t result = fread(buffer.data(), 1, size, file);

  fclose(file);

  // Read file
  if (result != size) {
    ESP_LOGE(TAG, "Failed to read config file, size mismatch");
    return false;
  }

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
  FILE* file = fopen(ConfigPath, "wb");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open config file for writing");
    return false;
  }

  auto& _rf            = s_mainConfig.rf;
  auto& _wifi          = s_mainConfig.wifi;
  auto& _backend       = s_mainConfig.backend;
  auto& _captivePortal = s_mainConfig.captivePortal;

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
  std::size_t size   = builder.GetSize();
  std::size_t result = fwrite(builder.GetBufferPointer(), 1, size, file);

  fclose(file);

  if (result != size) {
    ESP_LOGE(TAG, "Failed to write config file");
    return false;
  }

  return true;
}

void Config::Init() {
  s_fileSystem = FileSystem::GetConfig();
  if (!s_fileSystem->ok()) {
    ESP_LOGE(TAG, "Failed to mount config partition");
    return;
  }

  if (_tryLoadConfig()) {
    return;
  }

  ESP_LOGW(TAG, "Failed to load config, writing default config");

  s_mainConfig = {
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
    ESP_LOGE(TAG, "!!!CRITICAL ERROR!!! Failed to save default config. Reccomend formatting microcontroller and re-flashing firmware !!!CRITICAL ERROR!!!");
  }
}

void Config::FactoryReset() {
  if (unlink(ConfigPath) != 0) {
    ESP_LOGE(TAG, "!!!CRITICAL ERROR!!! Failed to remove existing config file for factory reset. Reccomend formatting microcontroller and re-flashing firmware !!!CRITICAL ERROR!!!");
  }
}

const Config::RFConfig& Config::GetRFConfig() {
  return s_mainConfig.rf;
}

const Config::WiFiConfig& Config::GetWiFiConfig() {
  return s_mainConfig.wifi;
}

const std::vector<Config::WiFiCredentials>& Config::GetWiFiCredentials() {
  return s_mainConfig.wifi.credentials;
}

const Config::CaptivePortalConfig& Config::GetCaptivePortalConfig() {
  return s_mainConfig.captivePortal;
}

const Config::BackendConfig& Config::GetBackendConfig() {
  return s_mainConfig.backend;
}

bool Config::SetRFConfig(const RFConfig& config) {
  s_mainConfig.rf = config;
  return _trySaveConfig();
}

bool Config::SetWiFiConfig(const WiFiConfig& config) {
  s_mainConfig.wifi = config;
  return _trySaveConfig();
}

bool Config::SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials) {
  for (auto& cred : credentials) {
    if (cred.id == 0) {
      ESP_LOGE(TAG, "Cannot set WiFi credentials: credential ID cannot be 0");
      return false;
    }
  }

  s_mainConfig.wifi.credentials = credentials;
  return _trySaveConfig();
}

bool Config::SetCaptivePortalConfig(const CaptivePortalConfig& config) {
  s_mainConfig.captivePortal = config;
  return _trySaveConfig();
}

bool Config::SetBackendConfig(const BackendConfig& config) {
  s_mainConfig.backend = config;
  return _trySaveConfig();
}

bool Config::SetRFConfigTxPin(std::uint8_t txPin) {
  s_mainConfig.rf.txPin = txPin;
  return _trySaveConfig();
}

std::uint8_t Config::AddWiFiCredentials(const std::string& ssid, const std::uint8_t (&bssid)[6], const std::string& password) {
  std::uint8_t id = 0;

  // Bitmask representing available credential IDs (0-31)
  std::uint32_t bits = 0;
  for (auto& creds : s_mainConfig.wifi.credentials) {
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
    while (bits & (1u << id) && id < 32) {
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

    s_mainConfig.wifi.credentials.push_back(creds);
  }

  _trySaveConfig();

  return id;
}

bool Config::TryGetWiFiCredentialsByID(std::uint8_t id, Config::WiFiCredentials& credentials) {
  if (id == 0) {
    return false;
  }

  for (auto& creds : s_mainConfig.wifi.credentials) {
    if (creds.id == id) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

bool Config::TryGetWiFiCredentialsBySSID(const char* ssid, Config::WiFiCredentials& credentials) {
  for (auto& creds : s_mainConfig.wifi.credentials) {
    if (creds.ssid == ssid) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

bool Config::TryGetWiFiCredentialsByBSSID(const std::uint8_t (&bssid)[6], Config::WiFiCredentials& credentials) {
  for (auto& creds : s_mainConfig.wifi.credentials) {
    if (memcmp(creds.bssid, bssid, 6) == 0) {
      credentials = creds;
      return true;
    }
  }

  return false;
}

void Config::RemoveWiFiCredentials(std::uint8_t id) {
  if (id == 0) {
    return;
  }

  for (auto it = s_mainConfig.wifi.credentials.begin(); it != s_mainConfig.wifi.credentials.end(); ++it) {
    if (it->id == id) {
      s_mainConfig.wifi.credentials.erase(it);
      _trySaveConfig();
      return;
    }
  }
}

void Config::ClearWiFiCredentials() {
  s_mainConfig.wifi.credentials.clear();
  _trySaveConfig();
}

bool Config::HasBackendAuthToken() {
  return !s_mainConfig.backend.authToken.empty();
}

const std::string& Config::GetBackendAuthToken() {
  return s_mainConfig.backend.authToken;
}

void Config::SetBackendAuthToken(const std::string& token) {
  s_mainConfig.backend.authToken = token;
  _trySaveConfig();
}

void Config::ClearBackendAuthToken() {
  s_mainConfig.backend.authToken = "";
  _trySaveConfig();
}
