#include "WiFiManager.h"

#include "CaptivePortal.h"
#include "VisualStateManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <vector>

const char* const TAG = "WiFiManager";

using namespace ShockLink;

struct WifiCredentials {
  String ssid;
  String password;
  std::uint16_t wifiIndex;
  std::uint8_t attempts;
};
static std::vector<WifiCredentials> s_wifiCredentials;
static WiFiState s_wifiState;

void SetWiFiState(WiFiState state) {
  s_wifiState = state;
  VisualStateManager::SetWiFiState(state);
}

bool SaveCredentials() {
  File file = LittleFS.open("/networks", FILE_WRITE);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open networks file for writing");
    return false;
  }

  DynamicJsonDocument doc(1024);
  JsonArray networks = doc.createNestedArray("networks");

  for (auto& cred : s_wifiCredentials) {
    JsonObject network  = networks.createNestedObject();
    network["ssid"]     = cred.ssid;
    network["password"] = cred.password;
  }

  if (serializeJson(doc, file) == 0) {
    ESP_LOGE(TAG, "Failed to serialize networks file");
    file.close();
    return false;
  }

  file.close();
  return true;
}
bool ReadCredentials() {
  File file = LittleFS.open("/networks", FILE_READ);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open networks file for reading");
    return false;
  }

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, file) != DeserializationError::Ok) {
    file.close();

    ESP_LOGE(TAG, "Failed to deserialize networks file, overwriting");
    SaveCredentials();

    return false;
  }

  s_wifiCredentials.clear();

  JsonArray networks = doc["networks"];
  for (int i = 0; i < networks.size(); i++) {
    JsonObject network = networks[i];
    String ssid        = network["ssid"];
    String password    = network["password"];

    s_wifiCredentials.push_back({ssid, password, UINT16_MAX, 0});
    ESP_LOGD(TAG, "Read credentials for %s", ssid.c_str());
  }

  file.close();

  return true;
}

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "Scan completed");

  std::uint16_t numNetworks = WiFi.scanComplete();
  if (numNetworks < 0) {
    if (numNetworks != WIFI_SCAN_RUNNING) {
      ESP_LOGE(TAG, "Scan failed");
      SetWiFiState(WiFiState::Disconnected);
      CaptivePortal::Start();
    } else {
      ESP_LOGE(TAG, "Scan is still running");
    }
    return;
  }

  DynamicJsonDocument doc(64 + numNetworks * 128);
  JsonArray networks = doc.createNestedArray("networks");

  if (numNetworks == 0) {
    ESP_LOGD(TAG, "No networks found");
    SetWiFiState(WiFiState::Disconnected);
    CaptivePortal::Start();
    CaptivePortal::BroadcastMessageJSON(doc);
    return;
  }

  for (auto& cred : s_wifiCredentials) {
    cred.wifiIndex = UINT16_MAX;
  }

  std::uint16_t recognizedNetworks = 0;
  for (std::uint16_t i = 0; i < numNetworks; i++) {
    String ssid = WiFi.SSID(i);
    bool saved  = false;
    for (auto& cred : s_wifiCredentials) {
      if (cred.ssid == ssid) {
        cred.wifiIndex = i;
        recognizedNetworks++;
        saved = true;
        break;
      }
    }
    JsonObject network = networks.createNestedObject();
    network["index"]   = i;
    network["ssid"]    = ssid;
    network["bssid"]   = WiFi.BSSIDstr(i);
    network["rssi"]    = WiFi.RSSI(i);
    network["channel"] = WiFi.channel(i);
    network["saved"]   = saved;
  }

  CaptivePortal::BroadcastMessageJSON(doc);

  if (recognizedNetworks == 0) {
    ESP_LOGD(TAG, "No recognized networks found");
    SetWiFiState(WiFiState::Disconnected);
    CaptivePortal::Start();
    return;
  }

  // Attempt to connect to the first recognized network
  for (auto& cred : s_wifiCredentials) {
    if (cred.wifiIndex != UINT16_MAX) {
      ESP_LOGD(TAG, "Attempting to connect to %s", cred.ssid.c_str());
      WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
      SetWiFiState(WiFiState::Connecting);
      return;
    }
  }
}
void _evWiFiConnected(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "WiFi connected");
  SetWiFiState(WiFiState::Connected);
  CaptivePortal::Stop();
}
void _evWiFiDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
  ESP_LOGD(TAG, "WiFi disconnected");
  SetWiFiState(WiFiState::Disconnected);
  CaptivePortal::Start();
}

bool WiFiManager::Init() {
  ReadCredentials();

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("ShockLink");  // TODO: Add the device name to the hostname (retrieve from API and store in LittleFS)

  if (s_wifiCredentials.size() > 0) {
    WiFi.scanNetworks(true);
    SetWiFiState(WiFiState::Scanning);
  } else {
    CaptivePortal::Start();
    SetWiFiState(WiFiState::Disconnected);
  }

  return true;
}

WiFiState WiFiManager::GetWiFiState() {
  return s_wifiState;
}

void WiFiManager::AddOrUpdateNetwork(const char* ssid, const char* password) {
  for (auto& cred : s_wifiCredentials) {
    if (cred.ssid == ssid) {
      cred.password = password;
      cred.attempts = 0;
      SaveCredentials();
      return;
    }
  }

  s_wifiCredentials.push_back({ssid, password, UINT16_MAX, 0});
  SaveCredentials();
}

void WiFiManager::RemoveNetwork(const char* ssid) {
  for (auto it = s_wifiCredentials.begin(); it != s_wifiCredentials.end(); it++) {
    if (it->ssid == ssid) {
      s_wifiCredentials.erase(it);
      SaveCredentials();
      return;
    }
  }
}

bool WiFiManager::StartScan() {
  if (s_wifiState != WiFiState::Disconnected) return false;

  CaptivePortal::BroadcastMessageTXT("{\"scanning\":true}");

  WiFi.scanNetworks(true);
  SetWiFiState(WiFiState::Scanning);

  return true;
}
