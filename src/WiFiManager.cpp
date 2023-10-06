#include "WiFiManager.h"

#include "CaptivePortal.h"
#include "Mappers/EspWiFiTypesMapper.h"
#include "Utils/HexUtils.h"
#include "VisualStateManager.h"
#include "WiFiCredentials.h"
#include "WiFiScanManager.h"

#include <ArduinoJson.h>
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
void _broadcastWifiConnectSuccess(const std::uint8_t (&bssid)[6]) {
  DynamicJsonDocument doc(64);
  doc["type"]    = "wifi";
  doc["subject"] = "connect";
  doc["status"]  = "success";
  doc["bssid"]   = HexUtils::ToHexMac<6>(bssid);
  CaptivePortal::BroadcastMessageJSON(doc);
}
void _broadcastWifiConnectError(const char* ssid, const std::uint8_t (&bssid)[6], const char* error) {
  DynamicJsonDocument doc(64);
  doc["type"]    = "wifi";
  doc["subject"] = "connect";
  doc["status"]  = "error";
  doc["ssid"]    = ssid;
  doc["bssid"]   = HexUtils::ToHexMac<6>(bssid);
  doc["error"]   = error;
  CaptivePortal::BroadcastMessageJSON(doc);
}

struct WiFiNetwork {
  char ssid[33];
  std::uint8_t bssid[6];
  std::uint8_t channel;
  std::int8_t rssi;
  wifi_auth_mode_t authMode;
  std::uint16_t reconnectionCount;
  std::uint8_t credentialsId;
};

static std::vector<WiFiNetwork> s_wifiNetworks;
static std::vector<WiFiCredentials> s_wifiCredentials;

bool _addNetwork(const char* ssid, std::uint8_t ssidLength, const char* password, std::uint8_t passwordLength) {
  // Bitmask representing available credential IDs (0-31)
  std::uint32_t bits = 0;
  for (auto& cred : s_wifiCredentials) {
    if (strcmp(cred.ssid().data(), ssid) == 0) {
      ESP_LOGE(TAG, "Failed to add WiFi credentials: credentials for %s already exist", ssid);
      cred.setPassword(password, passwordLength);
      cred.save();
      return true;
    }

    // Mark the credential ID as used
    bits |= 1u << cred.id();
  }

  // If we have 31 credentials, we can't add any more
  if (s_wifiCredentials.size() == 31) {
    ESP_LOGE(TAG, "Cannot add WiFi credentials: too many credentials");
    return false;
  }

  std::uint8_t id = 0;
  while (bits & (1u << id)) {
    id++;
  }

  WiFiCredentials credentials(id, ssid, ssidLength, password, passwordLength);
  credentials.save();

  s_wifiCredentials.push_back(std::move(credentials));

  return true;
}

void _evWiFiDisconnected(arduino_event_t* event) {
  auto& info = event->event_info.wifi_sta_disconnected;

  if (info.reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
    WiFi.disconnect(false);

    _broadcastWifiConnectError(reinterpret_cast<char*>(info.ssid), info.bssid, "authentication_failed");
    return;
  }
}
void _evWiFiNetworkDiscovered(const wifi_ap_record_t* record) {
  WiFiNetwork network {
    .ssid              = {0},
    .bssid             = {0},
    .channel           = record->primary,
    .rssi              = record->rssi,
    .authMode          = record->authmode,
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
    OpenShock::VisualStateManager::SetScanningStarted();
  }

  return true;
}

bool WiFiManager::Authenticate(std::uint8_t (&bssid)[6], const char* password, std::uint8_t passwordLength) {
  bool found = false;
  char ssid[33];
  for (std::uint16_t i = 0; i < s_wifiNetworks.size(); i++) {
    static_assert(sizeof(s_wifiNetworks[i].bssid) == sizeof(bssid), "BSSID size mismatch");
    if (memcmp(s_wifiNetworks[i].bssid, bssid, sizeof(bssid)) == 0) {
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

  if (!_addNetwork(ssid, strlen(ssid), password, passwordLength)) {
    _broadcastWifiAddNetworkError("too_many_credentials");
    return false;
  }

  _broadcastWifiAddNetworkSuccess(ssid);

  ESP_LOGI(TAG, "Added WiFi credentials for %s", ssid);
  wl_status_t stat = WiFi.begin(ssid, password, 0, bssid, true);
  if (stat != WL_CONNECTED) {
    ESP_LOGE(TAG, "Failed to connect to network %s, error code %d", ssid, stat);
    return false;
  }

  return true;
}

void WiFiManager::Forget(std::uint8_t wifiId) {
  // Check if the network is currently connected
  if (WiFi.isConnected()) {
    // Check if the network is the one we're connected to
    if (WiFi.SSID() == s_wifiCredentials[wifiId].ssid().data()) {
      // Disconnect from the network
      WiFi.disconnect(true);
    }
  }

  for (auto it = s_wifiCredentials.begin(); it != s_wifiCredentials.end(); it++) {
    if (it->id() == wifiId) {
      s_wifiCredentials.erase(it);
      it->erase();
      return;
    }
  }
}

void WiFiManager::Connect(std::uint8_t wifiId) {
  for (auto& creds : s_wifiCredentials) {
    if (creds.id() == wifiId) {
      ESP_LOGI(TAG, "Connecting to network #%u (%s)", wifiId, creds.ssid().data());
      WiFi.begin(creds.ssid().data(), creds.password().data());
      return;
    }
  }

  ESP_LOGE(TAG, "Failed to find credentials with ID %u", wifiId);
}

void WiFiManager::Disconnect() {
  WiFi.disconnect(true);
}
