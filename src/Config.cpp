#include "Config.h"

#include <esp_log.h>
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

Config::RFConfig FromFbsRFConfig(const Serialization::Configuration::RFConfig* fbsConfig) {
  if (fbsConfig == nullptr) {
#ifdef OPENSHOCK_TX_PIN
    return {.txPin = OPENSHOCK_TX_PIN};
#else
    ESP_LOGW(TAG, "OPENSHOCK_TX_PIN is not defined, defaulting to UINT32_MAX");
    return {.txPin = UINT32_MAX};
#endif
  }

  return {
    .txPin = fbsConfig->tx_pin(),
  };
}
Config::WiFiCredentials FromFbsWiFiCredentials(const Serialization::Configuration::WiFiCredentials* fbsConfig) {
  if (fbsConfig != nullptr) {
    auto id       = fbsConfig->id();
    auto ssid     = fbsConfig->ssid();
    auto bssid    = fbsConfig->bssid();
    auto password = fbsConfig->password();

    if (ssid != nullptr && bssid != nullptr && password != nullptr) {
      Config::WiFiCredentials creds {
        .id       = fbsConfig->id(),
        .ssid     = fbsConfig->ssid()->str(),
        .password = fbsConfig->password()->str(),
      };

      std::memcpy(creds.bssid, fbsConfig->bssid()->array()->data(), 6);

      return creds;
    }
  }

  return {
    .id       = 0,
    .ssid     = "",
    .bssid    = {0, 0, 0, 0, 0, 0},
    .password = "",
  };
}
Config::WiFiConfig FromFbsWiFiConfig(const Serialization::Configuration::WiFiConfig* fbsConfig) {
  if (fbsConfig != nullptr) {
    auto apSsid   = fbsConfig->ap_ssid();
    auto hostname = fbsConfig->hostname();
    auto creds    = fbsConfig->credentials();

    if (apSsid != nullptr && hostname != nullptr && creds != nullptr) {
      Config::WiFiConfig config {
        .apSsid   = fbsConfig->ap_ssid()->str(),
        .hostname = fbsConfig->hostname()->str(),
      };

      std::vector<Config::WiFiCredentials> credentials;

      for (auto it = creds->begin(); it != creds->end(); ++it) {
        credentials.push_back(FromFbsWiFiCredentials(*it));
      }

      config.credentials = std::move(credentials);

      return config;
    }
  }

  return {
    .apSsid      = "",
    .hostname    = "",
    .credentials = {},
  };
}
Config::CaptivePortalConfig FromFbsCaptivePortalConfig(const Serialization::Configuration::CaptivePortalConfig* fbsConfig) {
  if (fbsConfig != nullptr) {
    return {
      .alwaysEnabled = fbsConfig->always_enabled(),
    };
  }

  return {
    .alwaysEnabled = false,
  };
}
Config::BackendConfig FromFbsBackendConfig(const Serialization::Configuration::BackendConfig* fbsConfig) {
  if (fbsConfig != nullptr) {
    auto authToken = fbsConfig->auth_token();

    if (authToken != nullptr) {
      return {
        .authToken = fbsConfig->auth_token()->str(),
      };
    } else {
      ESP_LOGW(TAG, "authToken == nullptr");
    }
  } else {
    ESP_LOGW(TAG, "fbsConfig == nullptr");
  }

  return {
    .authToken = "",
  };
}
MainConfig FromFbsConfig(const Serialization::Configuration::Config* fbsConfig) {
  const Serialization::Configuration::RFConfig* rfConfig;
  const Serialization::Configuration::WiFiConfig* wifiConfig;
  const Serialization::Configuration::CaptivePortalConfig* captivePortalConfig;
  const Serialization::Configuration::BackendConfig* backendConfig;

  if (fbsConfig != nullptr) {
    rfConfig            = fbsConfig->rf();
    wifiConfig          = fbsConfig->wifi();
    captivePortalConfig = fbsConfig->captive_portal();
    backendConfig       = fbsConfig->backend();
  } else {
    rfConfig            = nullptr;
    wifiConfig          = nullptr;
    captivePortalConfig = nullptr;
    backendConfig       = nullptr;
  }

  return {
    .rf            = FromFbsRFConfig(rfConfig),
    .wifi          = FromFbsWiFiConfig(wifiConfig),
    .captivePortal = FromFbsCaptivePortalConfig(captivePortalConfig),
    .backend       = FromFbsBackendConfig(backendConfig),
  };
}

bool _tryLoadConfig(MainConfig& config) {
  File file = LittleFS.open("/config", "rb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open config file for reading");
    return false;
  }

  // Get file size
  std::size_t size = file.size();

  // Allocate buffer
  std::uint8_t* buffer = new std::uint8_t[size];

  // Read file
  if (file.read(buffer, size) != size) {
    delete[] buffer;
    ESP_LOGE(TAG, "Failed to read config file, size mismatch");
    return false;
  }

  file.close();

  // TODO: Implement checksum at end of file to verify integrity

  // Deserialize
  auto fbsConfig = flatbuffers::GetRoot<Serialization::Configuration::Config>(buffer);
  if (fbsConfig == nullptr) {
    delete[] buffer;
    ESP_LOGE(TAG, "Failed to get deserialization root for config file");
    return false;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {};
  flatbuffers::Verifier verifier(buffer, size, verifierOptions);
  if (!fbsConfig->Verify(verifier)) {
    delete[] buffer;
    ESP_LOGE(TAG, "Failed to verify config file integrity");
    return false;
  }

  // Copy
  config = FromFbsConfig(fbsConfig);

  // Cleanup
  delete[] buffer;

  return true;
}
bool _trySaveConfig(const MainConfig& config) {
  File file = LittleFS.open("/config", "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open config file for writing");
    return false;
  }

  // Serialize
  flatbuffers::FlatBufferBuilder builder(1024);

  auto rfConfig = Serialization::Configuration::RFConfig(config.rf.txPin);

  std::vector<flatbuffers::Offset<Serialization::Configuration::WiFiCredentials>> wifiCredentials;
  for (const auto& cred : config.wifi.credentials) {
    auto bssid = Serialization::Configuration::BSSID(cred.bssid);

    // WARNING: Passing the bssid by reference is probably not the way this is supposed to be done, will most likely segfault
    wifiCredentials.push_back(Serialization::Configuration::CreateWiFiCredentials(builder, cred.id, builder.CreateString(cred.ssid), &bssid, builder.CreateString(cred.password)));
  }

  auto wifiConfig = Serialization::Configuration::CreateWiFiConfig(builder, builder.CreateString(config.wifi.apSsid), builder.CreateString(config.wifi.hostname), builder.CreateVector(wifiCredentials));

  auto captivePortalConfig = Serialization::Configuration::CaptivePortalConfig(config.captivePortal.alwaysEnabled);

  auto backendConfig = Serialization::Configuration::CreateBackendConfig(builder, builder.CreateString(""), builder.CreateString(config.backend.authToken));

  // WARNING: Passing the bssid by reference is probably not the way this is supposed to be done, will most likely segfault
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
  if (!_tryLoadConfig(_mainConfig)) {
    ESP_LOGW(TAG, "Failed to load config, overwriting with default config");
    _mainConfig = {
      .rf = {
#ifdef OPENSHOCK_TX_PIN
        .txPin = OPENSHOCK_TX_PIN,
#else
        .txPin = UINT32_MAX,
#endif
      },
      .wifi = {
        .apSsid      = "Config",
        .hostname    = "Config",
        .credentials = {},
      },
      .captivePortal = {
        .alwaysEnabled = false,
      },
      .backend = {
        .authToken = "",
      },
    };
    _trySaveConfig(_mainConfig);
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

void Config::SetRFConfig(const RFConfig& config) {
  _mainConfig.rf = config;
  _trySaveConfig(_mainConfig);
}

void Config::SetWiFiConfig(const WiFiConfig& config) {
  _mainConfig.wifi = config;
  _trySaveConfig(_mainConfig);
}

void Config::SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials) {
  _mainConfig.wifi.credentials = credentials;
  _trySaveConfig(_mainConfig);
}

void Config::SetCaptivePortalConfig(const CaptivePortalConfig& config) {
  _mainConfig.captivePortal = config;
  _trySaveConfig(_mainConfig);
}

void Config::SetBackendConfig(const BackendConfig& config) {
  _mainConfig.backend = config;
  _trySaveConfig(_mainConfig);
}

std::uint8_t Config::AddWiFiCredentials(const std::string& ssid, std::uint8_t (&bssid)[6], const std::string& password) {
  std::uint8_t id = UINT8_MAX;

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

  if (id == UINT8_MAX) {
    id = 0;
    while (bits & (1u << id) && id < 32) {
      id++;
    }

    if (id >= 32) {
      ESP_LOGE(TAG, "Cannot add WiFi credentials: too many credentials");
      return UINT8_MAX;
    }

    WiFiCredentials creds {
      .id       = id,
      .ssid     = ssid,
      .password = password,
    };

    memcpy(creds.bssid, bssid, 6);

    _mainConfig.wifi.credentials.push_back(creds);
  }

  _trySaveConfig(_mainConfig);

  return id;
}

bool Config::TryGetWiFiCredentialsById(std::uint8_t id, Config::WiFiCredentials& credentials) {
  for (auto& creds : _mainConfig.wifi.credentials) {
    if (creds.id == id) {
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
      _trySaveConfig(_mainConfig);
      return;
    }
  }
}

void Config::ClearWiFiCredentials() {
  _mainConfig.wifi.credentials.clear();
  _trySaveConfig(_mainConfig);
}

bool Config::HasBackendAuthToken() {
  return !_mainConfig.backend.authToken.empty();
}

const std::string& Config::GetBackendAuthToken() {
  return _mainConfig.backend.authToken;
}

void Config::SetBackendAuthToken(const std::string& token) {
  _mainConfig.backend.authToken = token;
  _trySaveConfig(_mainConfig);
}

void Config::ClearBackendAuthToken() {
  _mainConfig.backend.authToken = "";
  _trySaveConfig(_mainConfig);
}
