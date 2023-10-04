#include "WiFiManager.h"

#include "CaptivePortal.h"
#include "WiFiCredentials.h"
#include "VisualStateManager.h"
#include "Mappers/EspWiFiTypesMapper.h"
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
}
void _broadcastWifiAddNetworkError(const char* error) {
  DynamicJsonDocument doc(64);
  doc["type"]    = "wifi";
  doc["subject"] = "add_network";
  doc["status"]  = "error";
  doc["error"]   = error;
}

struct WiFiStats {
  std::uint8_t credsId;
  std::uint16_t wifiIndex;
  std::uint16_t reconnectCount;
};

static std::vector<WiFiStats> s_wifiStats;
static std::vector<WiFiCredentials> s_wifiCredentials;

bool _addNetwork(const char* ssid, const String& password) {
  std::uint32_t bits = 0;
  for (auto& cred : s_wifiCredentials) {
    if (strcmp(cred.ssid().data(), ssid) == 0) {
      cred.setPassword(password);
      cred.save();
      return true;
    }
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

  s_wifiCredentials.push_back(WiFiCredentials(id, ssid, password));

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

bool WiFiManager::Init() {
  WiFiCredentials::Load(s_wifiCredentials);

  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

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
  char ssid[33]           = {0};
  std::uint16_t wifiIndex = UINT16_MAX;
  for (std::uint16_t i = 0; i < UINT16_MAX; i++) { // Yes, this is intentional (WiFi.getScanInfoByIndex returns nullptr when there are no more networks)
    wifi_ap_record_t* record = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    if (record == nullptr) {
      ESP_LOGE(TAG, "Failed to get scan info for network #%u", i);
      break;
    }

    if (memcmp(record->bssid, bssid.data(), bssid.size()) == 0) {
      wifiIndex = i;
      static_assert(sizeof(record->ssid) == sizeof(ssid), "SSID size mismatch");
      memcpy(ssid, record->ssid, sizeof(ssid));
      break;
    }
  }

  if (wifiIndex == UINT16_MAX) {
    ESP_LOGE(TAG, "Failed to find network with BSSID %02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    _broadcastWifiAddNetworkError("network_not_found");
    return false;
  }

  if (!_addNetwork(ssid, password)) {
    _broadcastWifiAddNetworkError("too_many_credentials");
    return false;
  }

  _broadcastWifiAddNetworkSuccess(ssid);

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
