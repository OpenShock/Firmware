#include "WiFiManager.h"

#include "CaptivePortal.h"
#include "WiFiCredentials.h"
#include "VisualStateManager.h"
#include "Mappers/EspWiFiTypesMapper.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <esp_wifi_types.h>

#include <vector>

#include <nonstd/span.hpp>

const char* const TAG = "WiFiManager";

using namespace OpenShock;

struct WiFiStats {
  std::uint8_t credsId;
  std::uint16_t wifiIndex;
  std::uint16_t reconnectCount;
};

static WiFiState s_wifiState;
static std::vector<WiFiStats> s_wifiStats;
static std::vector<WiFiCredentials> s_wifiCredentials;

void _setWiFiState(WiFiState state) {
  s_wifiState = state;
  VisualStateManager::SetWiFiState(state);
}

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

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "Scan completed");

  std::uint16_t numNetworks = WiFi.scanComplete();
  if (numNetworks < 0) {
    if (numNetworks != WIFI_SCAN_RUNNING) {
      ESP_LOGE(TAG, "Scan failed");
      _setWiFiState(WiFiState::Disconnected);
      CaptivePortal::Start();
      CaptivePortal::BroadcastMessageTXT("{\"type\":\"wifi\",\"subject\":\"scan\",\"status\":\"error\",\"error\":\"scan_failed\"}");
    } else {
      ESP_LOGE(TAG, "Scan is still running");
    }
    return;
  }

  DynamicJsonDocument doc(64 + numNetworks * 128);
  doc["type"]        = "wifi";
  doc["subject"]     = "scan";
  doc["status"]      = "completed";
  JsonArray networks = doc.createNestedArray("networks");

  if (numNetworks == 0) {
    ESP_LOGD(TAG, "No networks found");
    _setWiFiState(WiFiState::Disconnected);
    CaptivePortal::Start();
    CaptivePortal::BroadcastMessageJSON(doc);
    return;
  }

  std::vector<WiFiStats> oldStats = s_wifiStats;
  s_wifiStats.clear();

  for (std::uint16_t i = 0; i < numNetworks; i++) {
    wifi_ap_record_t* record = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    if (record == nullptr) {
      ESP_LOGE(TAG, "Failed to get scan info for network #%u", i);
      break;
    }

    // Fetch the saved credentials ID for this network
    std::uint8_t credsId = UINT8_MAX;
    for (auto& cred : s_wifiCredentials) {
      if (strcmp(cred.ssid().data(), reinterpret_cast<const char*>(record->ssid)) == 0) {
        credsId = cred.id();
        break;
      }
    }

    // If the credentials were saved, fetch or create the stats for this network
    std::uint16_t reconnectCount = UINT16_MAX;
    if (credsId != UINT8_MAX) {
      for (auto& stat : oldStats) {
        if (stat.credsId == credsId) {
          reconnectCount = stat.reconnectCount;
          break;
        }
      }

      if (reconnectCount == UINT16_MAX) {
        reconnectCount = 0;
      }

      s_wifiStats.push_back({ .credsId = credsId, .wifiIndex = i, .reconnectCount = reconnectCount });
    }

    char mac[18] = { 0 };
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", record->bssid[0], record->bssid[1], record->bssid[2], record->bssid[3], record->bssid[4], record->bssid[5]);

    JsonObject network = networks.createNestedObject();
    network["ssid"]     = record->ssid;
    network["bssid"]    = mac;
    network["rssi"]     = record->rssi;
    network["channel"]  = record->primary;
    network["security"] = Mappers::GetWiFiAuthModeName(record->authmode);
    network["saved"]    = credsId != UINT8_MAX;
  }

  CaptivePortal::BroadcastMessageJSON(doc);

  // Since wifiStats is a combination of detected networks and saved credentials, we can just check if it's empty to see if we recognized any networks
  if (s_wifiStats.empty()) {
    ESP_LOGD(TAG, "No recognized networks found");
    _setWiFiState(WiFiState::Disconnected);
    CaptivePortal::Start();
    return;
  }
}
void _evWiFiConnected(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "WiFi connected");
  _setWiFiState(WiFiState::Connected);
  CaptivePortal::Stop();
}
void _evWiFiDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "WiFi disconnected");
  _setWiFiState(WiFiState::Disconnected);
  CaptivePortal::Start();
}

bool WiFiManager::Init() {
  WiFiCredentials::Load(s_wifiCredentials);

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("OpenShock");  // TODO: Add the device name to the hostname (retrieve from API and store in LittleFS)

  if (s_wifiCredentials.size() > 0) {
    WiFi.scanNetworks(true);
    _setWiFiState(WiFiState::Scanning);
  } else {
    CaptivePortal::Start();
    _setWiFiState(WiFiState::Disconnected);
  }

  return true;
}

WiFiState WiFiManager::GetWiFiState() {
  return s_wifiState;
}

bool WiFiManager::Authenticate(nonstd::span<std::uint8_t, 6> bssid, const String& password) {
  char ssid[33] = { 0 };
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
    CaptivePortal::BroadcastMessageTXT("{\"type\":\"wifi\",\"status\":\"error\",\"error\":\"network_not_found\"}");
    return false;
  }

  if (!_addNetwork(ssid, password)) {
    CaptivePortal::BroadcastMessageTXT("{\"type\":\"wifi\",\"status\":\"error\",\"error\":\"too_many_credentials\"}");
    return false;
  }

  CaptivePortal::BroadcastMessageTXT(String("{\"type\":\"wifi\",\"status\":\"added\", \"ssid\":\"") + ssid + "\"}");

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
  if (s_wifiState != WiFiState::Disconnected) return;

  for (auto& creds : s_wifiCredentials) {
    if (creds.id() == wifiId) {
      WiFi.begin(creds.ssid().data(), creds.password().data());
      _setWiFiState(WiFiState::Connecting);
      return;
    }
  }

  ESP_LOGE(TAG, "Failed to find credentials with ID %u", wifiId);
}

void WiFiManager::Disconnect() {
  if (s_wifiState != WiFiState::Connected) return;

  WiFi.disconnect(true);
  _setWiFiState(WiFiState::Disconnected);
  CaptivePortal::Start();
}

bool WiFiManager::StartScan() {
  if (s_wifiState != WiFiState::Disconnected) return false;

  CaptivePortal::BroadcastMessageTXT("{\"type\":\"wifi\",\"subject\":\"scan\",\"status\":\"started\"}");

  WiFi.scanNetworks(true);
  _setWiFiState(WiFiState::Scanning);

  return true;
}
