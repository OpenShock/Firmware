#include "wifi/WiFiManager.h"

#include "CaptivePortal.h"
#include "Config.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "Time.h"
#include "VisualStateManager.h"
#include "wifi/WiFiNetwork.h"
#include "wifi/WiFiScanManager.h"

#include <WiFi.h>

#include <esp_wifi_types.h>

#include <vector>

#include <nonstd/span.hpp>

const char* const TAG = "WiFiManager";

using namespace OpenShock;

enum class WiFiState : std::uint8_t {
  Disconnected = 0,
  Connecting   = 1 << 0,
  Connected    = 1 << 1,
};

static WiFiState s_wifiState                 = WiFiState::Disconnected;
static std::uint8_t s_connectedBSSID[6]      = {0};
static std::uint8_t s_connectedCredentialsID = 0;
static std::uint8_t s_preferredCredentialsID = 0;
static std::vector<WiFiNetwork> s_wifiNetworks;

bool _isZeroBSSID(const std::uint8_t (&bssid)[6]) {
  for (std::size_t i = 0; i < sizeof(bssid); i++) {
    if (bssid[i] != 0) {
      return false;
    }
  }

  return true;
}

bool _attractivityComparer(const WiFiNetwork& a, const WiFiNetwork& b) {
  if (a.credentialsID == 0) {
    return false;
  }

  if (a.connectAttempts > b.connectAttempts) {
    return false;
  }

  return a.rssi > b.rssi;
}
bool _isConnectRateLimited(const WiFiNetwork& net) {
  if (net.lastConnectAttempt == 0) {
    return false;
  }

  std::int64_t now  = OpenShock::millis();
  std::int64_t diff = now - net.lastConnectAttempt;
  if ((net.connectAttempts > 5 && diff < 5000) || (net.connectAttempts > 10 && diff < 10'000) || (net.connectAttempts > 15 && diff < 30'000) || (net.connectAttempts > 20 && diff < 60'000)) {
    return true;
  }

  return false;
}

bool _isSaved(std::function<bool(const Config::WiFiCredentials&)> predicate) {
  const auto& credentials = Config::GetWiFiCredentials();

  return std::any_of(credentials.begin(), credentials.end(), predicate);
}
std::vector<WiFiNetwork>::iterator _findNetwork(std::function<bool(WiFiNetwork&)> predicate, bool sortByAttractivity = true) {
  if (sortByAttractivity) {
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), _attractivityComparer);
  }
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}
std::vector<WiFiNetwork>::iterator _findNetworkBySSID(const char* ssid, bool sortByAttractivity = true) {
  return _findNetwork([ssid](const WiFiNetwork& net) { return strcmp(net.ssid, ssid) == 0; }, sortByAttractivity);
}
std::vector<WiFiNetwork>::iterator _findNetworkByBSSID(const std::uint8_t (&bssid)[6]) {
  return _findNetwork([bssid](const WiFiNetwork& net) { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; }, false);
}
std::vector<WiFiNetwork>::iterator _findNetworkByCredentialsID(std::uint8_t credentialsID, bool sortByAttractivity = true) {
  return _findNetwork([credentialsID](const WiFiNetwork& net) { return net.credentialsID == credentialsID; }, sortByAttractivity);
}

bool _markNetworkAsAttempted(const std::uint8_t (&bssid)[6]) {
  auto it = _findNetworkByBSSID(bssid);
  if (it == s_wifiNetworks.end()) {
    return false;
  }

  it->connectAttempts++;
  it->lastConnectAttempt = OpenShock::millis();

  return true;
}

bool _getNextWiFiNetwork(OpenShock::Config::WiFiCredentials& creds) {
  return _findNetwork([&creds](const WiFiNetwork& net) {
    if (net.credentialsID == 0) {
      return false;
    }

    if (_isConnectRateLimited(net)) {
      return false;
    }

    if (!Config::TryGetWiFiCredentialsByID(net.credentialsID, creds)) {
      return false;
    }

    memcpy(creds.bssid, net.bssid, sizeof(creds.bssid));

    return true;
  }) != s_wifiNetworks.end();
}

bool _authenticate(const WiFiNetwork& net, const std::string& password) {
  std::uint8_t id = Config::AddWiFiCredentials(net.ssid, net.bssid, password);
  if (id == 0) {
    Serialization::Local::SerializeErrorMessage("too_many_credentials", CaptivePortal::BroadcastMessageBIN);
    return false;
  }

  Serialization::Local::SerializeWiFiNetworkSavedEvent(net, CaptivePortal::BroadcastMessageBIN);

  ESP_LOGI(TAG, "Added WiFi credentials for %s", net.ssid);
  wl_status_t stat = WiFi.begin(net.ssid, password.c_str(), 0, net.bssid, true);
  if (stat == WL_CONNECT_FAILED) {
    ESP_LOGE(TAG, "Failed to connect to network %s, error code %d", net.ssid, stat);
    return false;
  }

  return true;
}

bool _connect(const char* ssid, const char* password, const std::uint8_t (&bssid)[6]) {
  ESP_LOGV(TAG, "Connecting to network %s (" BSSID_FMT ")", ssid, BSSID_ARG(bssid));

  _markNetworkAsAttempted(bssid);

  // Connect to the network
  s_wifiState = WiFiState::Connecting;
  if (WiFi.begin(ssid, password, 0, bssid, true) == WL_CONNECT_FAILED) {
    s_wifiState = WiFiState::Disconnected;
    return false;
  }

  return true;
}
bool _connectHidden(const Config::WiFiCredentials& creds) {
  ESP_LOGV(TAG, "Connecting to hidden network " BSSID_FMT, BSSID_ARG(creds.bssid));

  // TODO: Implement hidden network support
  ESP_LOGE(TAG, "Connecting to hidden networks is not yet supported");

  return false;
}
bool _connect(const Config::WiFiCredentials& creds) {
  if (creds.ssid.empty()) {
    return _connectHidden(creds);
  }

  if (!_isZeroBSSID(creds.bssid)) {
    return _connect(creds.ssid.c_str(), creds.password.c_str(), creds.bssid);
  }

  auto it = _findNetworkBySSID(creds.ssid.c_str());
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", creds.ssid.c_str());
    return false;
  }

  return _connect(creds.ssid.c_str(), creds.password.c_str(), it->bssid);
}

void _evWiFiConnected(arduino_event_t* event) {
  auto& info = event->event_info.wifi_sta_connected;

  s_wifiState = WiFiState::Connected;
  memcpy(s_connectedBSSID, info.bssid, sizeof(s_connectedBSSID));

  auto it = _findNetworkByBSSID(info.bssid);
  if (it == s_wifiNetworks.end()) {
    s_connectedCredentialsID = 0;

    ESP_LOGW(TAG, "Connected to unknown network " BSSID_FMT, BSSID_ARG(info.bssid));

    return;
  }

  s_connectedCredentialsID = it->credentialsID;

  ESP_LOGI(TAG, "Connected to network %s (" BSSID_FMT ")", reinterpret_cast<const char*>(info.ssid), BSSID_ARG(info.bssid));

  Serialization::Local::SerializeWiFiNetworkConnectedEvent(*it, CaptivePortal::BroadcastMessageBIN);
}
void _evWiFiDisconnected(arduino_event_t* event) {
  s_wifiState = WiFiState::Disconnected;

  auto& info = event->event_info.wifi_sta_disconnected;

  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(reinterpret_cast<char*>(info.ssid), creds) && !Config::TryGetWiFiCredentialsByBSSID(info.bssid, creds)) {
    ESP_LOGW(TAG, "Disconnected from unknown network... WTF?");
    return;
  }

  ESP_LOGI(TAG, "Disconnected from network %s (" BSSID_FMT ")", info.ssid, BSSID_ARG(info.bssid));

  if (info.reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || info.reason == WIFI_REASON_AUTH_EXPIRE || info.reason == WIFI_REASON_AUTH_FAIL) {
    Serialization::Local::SerializeErrorMessage("WiFi authentication failed", CaptivePortal::BroadcastMessageBIN);
  } else {
    char reason[64];
    snprintf(reason, sizeof(reason), "WiFi connection failed (reason %d)", info.reason);
    Serialization::Local::SerializeErrorMessage(reason, CaptivePortal::BroadcastMessageBIN);
  }
}
void _evWiFiScanStarted() { }
void _evWiFiScanStatusChanged(OpenShock::WiFiScanStatus status) {
  // If the scan started, remove any networks that have not been seen in 3 scans
  if (status == OpenShock::WiFiScanStatus::Started) {
    for (auto it = s_wifiNetworks.begin(); it != s_wifiNetworks.end(); ++it) {
      if (it->scansMissed++ > 3) {
        ESP_LOGV(TAG, "Network %s (" BSSID_FMT ") has not been seen in 3 scans, removing from list", it->ssid, BSSID_ARG(it->bssid));
        Serialization::Local::SerializeWiFiNetworkLostEvent(*it, CaptivePortal::BroadcastMessageBIN);
        s_wifiNetworks.erase(it);
        it--;
      }
    }
  }

  // If the scan completed, sort the networks by RSSI
  if (status == OpenShock::WiFiScanStatus::Completed || status == OpenShock::WiFiScanStatus::Aborted || status == OpenShock::WiFiScanStatus::Error) {
    // Sort the networks by RSSI
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), [](const WiFiNetwork& a, const WiFiNetwork& b) { return a.rssi > b.rssi; });
  }
}
void _evWiFiNetworkDiscovery(const wifi_ap_record_t* record) {
  auto it = _findNetworkByBSSID(record->bssid);
  if (it != s_wifiNetworks.end()) {
    // Update the network
    memcpy(it->ssid, record->ssid, sizeof(it->ssid));
    it->channel     = record->primary;
    it->rssi        = record->rssi;
    it->authMode    = record->authmode;
    it->scansMissed = 0;

    Serialization::Local::SerializeWiFiNetworkUpdatedEvent(*it, CaptivePortal::BroadcastMessageBIN);
    ESP_LOGV(TAG, "Updated network %s (" BSSID_FMT ") with new scan info", it->ssid, BSSID_ARG(it->bssid));

    return;
  }

  WiFiNetwork network(record->ssid, record->bssid, record->primary, record->rssi, record->authmode, 0);

  Config::WiFiCredentials creds;
  if (Config::TryGetWiFiCredentialsBySSID(reinterpret_cast<const char*>(record->ssid), creds) || Config::TryGetWiFiCredentialsByBSSID(record->bssid, creds)) {
    network.credentialsID = creds.id;
  }

  Serialization::Local::SerializeWiFiNetworkDiscoveredEvent(network, CaptivePortal::BroadcastMessageBIN);
  ESP_LOGV(TAG, "Discovered new network %s (" BSSID_FMT ")", network.ssid, BSSID_ARG(network.bssid));

  // Insert the network into the list of networks sorted by RSSI
  s_wifiNetworks.insert(std::lower_bound(s_wifiNetworks.begin(), s_wifiNetworks.end(), network, [](const WiFiNetwork& a, const WiFiNetwork& b) { return a.rssi > b.rssi; }), std::move(network));
}

bool WiFiManager::Init() {
  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFiScanManager::RegisterStatusChangedHandler(_evWiFiScanStatusChanged);
  WiFiScanManager::RegisterNetworkDiscoveryHandler(_evWiFiNetworkDiscovery);

  if (!WiFiScanManager::Init()) {
    ESP_LOGE(TAG, "Failed to initialize WiFiScanManager");
    return false;
  }

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.enableSTA(true);
  WiFi.setHostname(OPENSHOCK_FW_HOSTNAME);  // TODO: Add the device name to the hostname (retrieve from API and store in LittleFS)

  return true;
}

bool WiFiManager::Save(const char* ssid, const std::string& password) {
  ESP_LOGV(TAG, "Authenticating to network %s", ssid);

  auto it = _findNetworkBySSID(ssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", ssid);

    Serialization::Local::SerializeErrorMessage("network_not_found", CaptivePortal::BroadcastMessageBIN);

    return false;
  }

  return _authenticate(*it, password);
}

bool WiFiManager::Save(const std::uint8_t (&bssid)[6], const std::string& password) {
  ESP_LOGV(TAG, "Authenticating to network " BSSID_FMT, BSSID_ARG(bssid));

  auto it = _findNetworkByBSSID(bssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with BSSID " BSSID_FMT, BSSID_ARG(bssid));

    Serialization::Local::SerializeErrorMessage("network_not_found", CaptivePortal::BroadcastMessageBIN);

    return false;
  }

  return _authenticate(*it, password);
}

bool WiFiManager::Forget(const char* ssid) {
  ESP_LOGV(TAG, "Forgetting network %s", ssid);

  auto it = _findNetworkBySSID(ssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", ssid);
    return false;
  }

  std::uint8_t credsId = it->credentialsID;

  // Check if the network is currently connected
  if (s_connectedCredentialsID == credsId) {
    // Disconnect from the network
    WiFiManager::Disconnect();
  }

  // Remove the credentials from the config
  Config::RemoveWiFiCredentials(credsId);

  return true;
}

bool WiFiManager::Forget(const std::uint8_t (&bssid)[6]) {
  ESP_LOGV(TAG, "Forgetting network " BSSID_FMT, BSSID_ARG(bssid));

  auto it = std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), [bssid](const WiFiNetwork& net) { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; });
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with BSSID " BSSID_FMT, BSSID_ARG(bssid));
    return false;
  }

  std::uint8_t credsId = it->credentialsID;

  // Check if the network is currently connected
  if (s_connectedCredentialsID == credsId) {
    // Disconnect from the network
    WiFiManager::Disconnect();
  }

  // Remove the credentials from the config
  Config::RemoveWiFiCredentials(credsId);

  return true;
}

bool WiFiManager::RefreshNetworkCredentials() {
  ESP_LOGV(TAG, "Refreshing network credentials");

  for (auto& net : s_wifiNetworks) {
    Config::WiFiCredentials creds;
    if (Config::TryGetWiFiCredentialsBySSID(net.ssid, creds) || Config::TryGetWiFiCredentialsByBSSID(net.bssid, creds)) {
      ESP_LOGV(TAG, "Found credentials for network %s (" BSSID_FMT ")", net.ssid, BSSID_ARG(net.bssid));
      net.credentialsID = creds.id;
    } else {
      ESP_LOGV(TAG, "Failed to find credentials for network %s (" BSSID_FMT ")", net.ssid, BSSID_ARG(net.bssid));
      net.credentialsID = 0;
    }
  }

  return true;
}

bool WiFiManager::IsSaved(const char* ssid) {
  return _isSaved([ssid](const Config::WiFiCredentials& creds) { return creds.ssid == ssid; });
}

bool WiFiManager::IsSaved(const std::uint8_t (&bssid)[6]) {
  return _isSaved([bssid](const Config::WiFiCredentials& creds) { return memcmp(creds.bssid, bssid, sizeof(bssid)) == 0; });
}

bool WiFiManager::IsSaved(const char* ssid, const std::uint8_t (&bssid)[6]) {
  return _isSaved([ssid, bssid](const Config::WiFiCredentials& creds) { return creds.ssid == ssid || memcmp(creds.bssid, bssid, sizeof(bssid)) == 0; });
}

bool WiFiManager::Connect(const char* ssid) {
  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(ssid, creds)) {
    ESP_LOGE(TAG, "Failed to find credentials for network %s", ssid);
    return false;
  }

  if (s_connectedCredentialsID != creds.id) {
    Disconnect();
    s_preferredCredentialsID = creds.id;
    return true;
  }

  if (s_wifiState == WiFiState::Disconnected) {
    s_preferredCredentialsID = creds.id;
    return true;
  }

  return false;
}

bool WiFiManager::Connect(const std::uint8_t (&bssid)[6]) {
  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsByBSSID(bssid, creds)) {
    ESP_LOGE(TAG, "Failed to find credentials for network " BSSID_FMT, BSSID_ARG(bssid));
    return false;
  }

  if (s_connectedCredentialsID != creds.id) {
    Disconnect();
    s_preferredCredentialsID = creds.id;
    return true;
  }

  if (s_wifiState == WiFiState::Disconnected) {
    s_preferredCredentialsID = creds.id;
    return true;
  }

  return false;
}

void WiFiManager::Disconnect() {
  WiFi.disconnect(false);
}

bool WiFiManager::IsConnected() {
  return s_wifiState == WiFiState::Connected;
}
bool WiFiManager::GetConnectedNetwork(OpenShock::WiFiNetwork& network) {
  if (s_connectedCredentialsID == 0) {
    return false;
  }

  auto it = _findNetwork([](const WiFiNetwork& net) { return net.credentialsID == s_connectedCredentialsID; });
  if (it == s_wifiNetworks.end()) {
    return false;
  }

  network = *it;

  return true;
}

static std::int64_t s_lastScanRequest = 0;
void WiFiManager::Update() {
  if (s_wifiState != WiFiState::Disconnected || WiFiScanManager::IsScanning()) return;

  if (s_preferredCredentialsID != 0) {
    Config::WiFiCredentials creds;
    bool foundCreds = Config::TryGetWiFiCredentialsByID(s_preferredCredentialsID, creds);

    s_preferredCredentialsID = 0;

    if (!foundCreds) {
      ESP_LOGE(TAG, "Failed to find credentials with ID %u", s_preferredCredentialsID);
      return;
    }

    if (_connect(creds)) {
      return;
    }

    ESP_LOGE(TAG, "Failed to connect to network %s (" BSSID_FMT ")", creds.ssid.c_str(), BSSID_ARG(creds.bssid));
  }

  Config::WiFiCredentials creds;
  if (!_getNextWiFiNetwork(creds)) {
    std::int64_t now = OpenShock::millis();
    if (s_lastScanRequest == 0 || now - s_lastScanRequest > 30'000) {
      s_lastScanRequest = now;

      ESP_LOGV(TAG, "No networks to connect to, starting scan...");
      WiFiScanManager::StartScan();
    }
    return;
  }

  _connect(creds);
}
