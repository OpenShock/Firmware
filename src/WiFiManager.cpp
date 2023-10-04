#include "WiFiManager.h"

#include "CaptivePortal.h"
#include "Mappers/EspWiFiTypesMapper.h"
#include "VisualStateManager.h"
#include "WiFiCredentials.h"
#include "WiFiScanManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <esp_wifi_types.h>

#include <vector>

#include <nonstd/span.hpp>

const char* const TAG = "WiFiManager";

using namespace OpenShock;

void _broadcastWifiAddNetworkSuccess(const char* ssid) {
  DynamicJsonDocument doc(64);
  doc["type"]    = "wifi";
  doc["subject"] = "add_network";
  doc["status"]  = "success";
  doc["ssid"]    = ssid;
  CaptivePortal::BroadcastMessageJSON(doc);
}
void _broadcastWifiAddNetworkError(const char* error) {
  DynamicJsonDocument doc(64);
  doc["type"]    = "wifi";
  doc["subject"] = "add_network";
  doc["status"]  = "error";
  doc["error"]   = error;
  CaptivePortal::BroadcastMessageJSON(doc);
}

struct WiFiNetwork {
  char ssid[33];
  std::uint8_t bssid[6];
  std::uint8_t channel;
  std::int8_t rssi;
  wifi_auth_mode_t authmode;
  std::uint16_t reconnectionCount;
  std::uint8_t credentialsId;
};

static std::vector<WiFiNetwork> s_wifiNetworks;
static std::vector<WiFiCredentials> s_wifiCredentials;

bool _addNetwork(const char* ssid, const String& password) {
  // Bitmask representing available credential IDs (0-31)
  std::uint32_t bits = 0;
  for (auto& cred : s_wifiCredentials) {
    if (strcmp(cred.ssid().data(), ssid) == 0) {
      ESP_LOGE(TAG, "Failed to add WiFi credentials: credentials for %s already exist", ssid);
      cred.setPassword(password);
      cred.save();
      return true;
    }

    // Mark the credential ID as used
    bits |= 1u << cred.id();
  }

  // If we have 255 credentials, we can't add any more
  if (s_wifiCredentials.size() == 255) {
    ESP_LOGE(TAG, "Cannot add WiFi credentials: too many credentials");
    return false;
  }

  std::uint8_t id = 0;
  while (bits & (1u << id)) {
    id++;
  }

  WiFiCredentials credentials(id, ssid, password);
  credentials.save();

  s_wifiCredentials.push_back(std::move(credentials));

  return true;
}

void _evWiFiConnected(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "WiFi connected");
  OpenShock::SetWiFiState(WiFiState::Connected);
  CaptivePortal::Stop();
}
void _evWiFiDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "WiFi disconnected");
  OpenShock::SetWiFiState(WiFiState::Disconnected);
  CaptivePortal::Start();
}
void _evWiFiNetworkDiscovered(const wifi_ap_record_t* record) {
  WiFiNetwork network {
    .ssid              = {0},
    .bssid             = {0},
    .channel           = record->primary,
    .rssi              = record->rssi,
    .authmode          = record->authmode,
    .reconnectionCount = 0,
    .credentialsId     = UINT8_MAX,
  };

  static_assert(sizeof(network.ssid) == sizeof(record->ssid), "SSID size mismatch");
  memcpy(network.ssid, record->ssid, sizeof(network.ssid));

  static_assert(sizeof(network.bssid) == sizeof(record->bssid), "BSSID size mismatch");
  memcpy(network.bssid, record->bssid, sizeof(network.bssid));

  s_wifiNetworks.push_back(network);
}

bool WiFiManager::Init() {
  WiFiCredentials::Load(s_wifiCredentials);

  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFiScanManager::RegisterScanDiscoveryHandler(_evWiFiNetworkDiscovered);

  if (!WiFiScanManager::Init()) {
    ESP_LOGE(TAG, "Failed to initialize WiFiScanManager");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("OpenShock");  // TODO: Add the device name to the hostname (retrieve from API and store in LittleFS)

  if (s_wifiCredentials.size() > 0) {
    WiFi.scanNetworks(true);
    OpenShock::SetWiFiState(WiFiState::Scanning);
  } else {
    CaptivePortal::Start();
    OpenShock::SetWiFiState(WiFiState::Disconnected);
  }

  return true;
}

bool WiFiManager::Authenticate(nonstd::span<std::uint8_t, 6> bssid, const String& password) {
  bool found = false;
  char ssid[33];
  for (std::uint16_t i = 0; i < s_wifiNetworks.size(); i++) {
    if (memcmp(s_wifiNetworks[i].bssid, bssid.data(), bssid.size()) == 0) {
      memcpy(ssid, s_wifiNetworks[i].ssid, sizeof(ssid));
      found = true;
      break;
    }
  }

  if (!found) {
    ESP_LOGE(TAG, "Failed to find network with BSSID %02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    _broadcastWifiAddNetworkError("network_not_found");
    return false;
  }

  if (!_addNetwork(ssid, password)) {
    _broadcastWifiAddNetworkError("too_many_credentials");
    return false;
  }

  _broadcastWifiAddNetworkSuccess(ssid);

  wl_status_t stat = WiFi.begin(ssid, password, 0, bssid.data(), true);
  if (stat != WL_CONNECTED) {
    ESP_LOGE(TAG, "Failed to connect to network %s, error code %d", ssid, stat);
    return false;
  }

  return true;
}

void WiFiManager::Forget(std::uint8_t wifiId) {
  for (auto it = s_wifiCredentials.begin(); it != s_wifiCredentials.end(); it++) {
    if (it->id() == wifiId) {
      s_wifiCredentials.erase(it);
      it->erase();
      return;
    }
  }
}

void WiFiManager::Connect(std::uint8_t wifiId) {
  if (OpenShock::GetWiFiState() != WiFiState::Disconnected) return;

  for (auto& creds : s_wifiCredentials) {
    if (creds.id() == wifiId) {
      WiFi.begin(creds.ssid().data(), creds.password().data());
      OpenShock::SetWiFiState(WiFiState::Connecting);
      return;
    }
  }

  ESP_LOGE(TAG, "Failed to find credentials with ID %u", wifiId);
}

void WiFiManager::Disconnect() {
  if (OpenShock::GetWiFiState() != WiFiState::Connected) return;

  WiFi.disconnect(true);
  OpenShock::SetWiFiState(WiFiState::Disconnected);
  CaptivePortal::Start();
}
