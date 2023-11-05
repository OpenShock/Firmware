#include "WiFiManager.h"

#include "CaptivePortal.h"
#include "Config.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "Mappers/EspWiFiTypesMapper.h"
#include "Time.h"
#include "Utils/HexUtils.h"
#include "VisualStateManager.h"
#include "WiFiNetwork.h"
#include "WiFiScanManager.h"

#include "_fbs/DeviceToLocalMessage_generated.h"

#include <WiFi.h>

#include <esp_wifi_types.h>

#include <vector>

#include <nonstd/span.hpp>

const char* const TAG = "WiFiManager";

using namespace OpenShock;

flatbuffers::Offset<OpenShock::Serialization::Local::WifiNetwork> _createWiFiNetwork(flatbuffers::FlatBufferBuilder& builder, const WiFiNetwork& network) {
  char bssid[18];
  HexUtils::ToHexMac<6>(network.bssid, bssid);

  return Serialization::Local::CreateWifiNetworkDirect(builder, network.ssid, bssid, network.channel, network.rssi, network.authMode, network.credentialsID != 0);
}

void _broadcastWifiNetworkDiscovered(const WiFiNetwork& network) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Serialization::Local::CreateWifiNetworkDiscoveredEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkDiscoveredEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}
void _broadcastWifiNetworkUpdated(const WiFiNetwork& network) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Serialization::Local::CreateWifiNetworkUpdatedEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkUpdatedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}
void _broadcastWifiNetworkLost(const WiFiNetwork& network) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Serialization::Local::CreateWifiNetworkLostEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkLostEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}

void _broadcastWifiNetworkSaved(const WiFiNetwork& network) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Serialization::Local::CreateWifiNetworkSavedEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkSavedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}

void _broadcastErrorMessage(const char* error) {
  std::size_t errorLen = strlen(error);

  flatbuffers::FlatBufferBuilder builder(32 + errorLen);  // TODO: Profile this and adjust the size accordingly

  auto errorOffset = builder.CreateString(error, errorLen);

  auto wrapperOffset = Serialization::Local::CreateErrorMessage(builder, errorOffset);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::ErrorMessage, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}
void _broadcastWifiConnected(const WiFiNetwork& network) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Serialization::Local::CreateWifiNetworkConnectedEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkConnectedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}

static bool s_wifiConnected                  = false;
static bool s_wifiConnecting                 = false;
static std::uint8_t s_connectedCredentialsID = 0;
static std::uint8_t s_preferredCredentialsID = 0;
static std::vector<WiFiNetwork> s_wifiNetworks;

/// @brief Gets the next WiFi network to connect to
///
/// This function will return the next WiFi network to connect to, based on the following criteria:
/// - The network must be saved in the config
/// - The network must not have reached the maximum number of connection attempts
/// - The network must have the least number of connection attempts
/// @param network The network to populate with the next WiFi network to connect to
/// @return True if a network was found, false otherwise
bool GetNextWiFiNetwork(OpenShock::Config::WiFiCredentials& creds) {
  std::int64_t now = OpenShock::millis();

  bool found                         = false;
  std::int8_t highestRssi            = INT8_MIN;
  std::uint16_t leastConnectAttempts = UINT16_MAX;
  for (auto& net : s_wifiNetworks) {
    if (net.credentialsID == 0) continue;
    if (net.connectAttempts > leastConnectAttempts) continue;

    if (net.connectAttempts < leastConnectAttempts) {
      leastConnectAttempts = net.connectAttempts;
    } else if (net.rssi > highestRssi) {
      ESP_LOGV(TAG, "Network %s (" BSSID_FMT ") has the highest RSSI (%d)", net.ssid, BSSID_ARG(net.bssid), net.rssi);
      highestRssi = net.rssi;
    } else {
      ESP_LOGV(TAG, "Network %s (" BSSID_FMT ") is not a candidate", net.ssid, BSSID_ARG(net.bssid));
      continue;
    }

    if (net.lastConnectAttempt != 0) {
      std::int64_t diff = now - net.lastConnectAttempt;
      if ((net.connectAttempts > 5 && diff < 5000) || (net.connectAttempts > 10 && diff < 10'000) || (net.connectAttempts > 15 && diff < 30'000) || (net.connectAttempts > 20 && diff < 60'000)) {
        continue;
      }
    }

    if (!Config::TryGetWiFiCredentialsByID(net.credentialsID, creds)) {
      ESP_LOGE(TAG, "Failed to find credentials with ID %u", net.credentialsID);
      net.credentialsID = 0;
      continue;
    }

    found = true;
  }

  for (auto& net : s_wifiNetworks) {
    if (net.credentialsID == creds.id) {
      net.connectAttempts++;
      net.lastConnectAttempt = now;
      break;
    }
  }

  return found;
}

std::vector<WiFiNetwork>::iterator _findNetwork(std::function<bool(const WiFiNetwork&)> predicate) {
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}
std::vector<WiFiNetwork>::iterator _findNetwork(const char* ssid) {
  return _findNetwork([ssid](const WiFiNetwork& net) { return strcmp(net.ssid, ssid) == 0; });
}
std::vector<WiFiNetwork>::iterator _findNetwork(const std::uint8_t (&bssid)[6]) {
  return _findNetwork([bssid](const WiFiNetwork& net) { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; });
}

void _evWiFiConnected(arduino_event_t* event) {
  s_wifiConnected  = true;
  s_wifiConnecting = false;

  auto& info = event->event_info.wifi_sta_connected;

  auto it = _findNetwork(info.bssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGW(TAG, "Connected to unknown network " BSSID_FMT, BSSID_ARG(info.bssid));
    return;
  }

  ESP_LOGI(TAG, "Connected to network %s (" BSSID_FMT ")", it->ssid, BSSID_ARG(it->bssid));

  s_connectedCredentialsID = it->credentialsID;
  _broadcastWifiConnected(*it);
}
void _evWiFiDisconnected(arduino_event_t* event) {
  s_wifiConnected          = false;
  s_wifiConnecting         = false;
  s_connectedCredentialsID = 0;

  auto& info = event->event_info.wifi_sta_disconnected;

  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(reinterpret_cast<char*>(info.ssid), creds) && !Config::TryGetWiFiCredentialsByBSSID(info.bssid, creds)) {
    ESP_LOGW(TAG, "Disconnected from unknown network... WTF?");
    return;
  }

  ESP_LOGI(TAG, "Disconnected from network %s (" BSSID_FMT ")", info.ssid, BSSID_ARG(info.bssid));

  if (info.reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || info.reason == WIFI_REASON_AUTH_EXPIRE || info.reason == WIFI_REASON_AUTH_FAIL) {
    _broadcastErrorMessage("WiFi authentication failed");
  } else {
    char reason[64];
    snprintf(reason, sizeof(reason), "WiFi connection failed (reason %d)", info.reason);
    _broadcastErrorMessage(reason);
  }
}
void _evWiFiScanStarted() { }
void _evWiFiScanStatusChanged(OpenShock::WiFiScanStatus status) {
  // If the scan started, remove any networks that have not been seen in 3 scans
  if (status == OpenShock::WiFiScanStatus::Started) {
    for (auto it = s_wifiNetworks.begin(); it != s_wifiNetworks.end(); ++it) {
      if (it->scansMissed++ > 3) {
        ESP_LOGV(TAG, "Network %s (" BSSID_FMT ") has not been seen in 3 scans, removing from list", it->ssid, BSSID_ARG(it->bssid));
        _broadcastWifiNetworkLost(*it);
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
  OpenShock::WiFiAuthMode authMode = Mappers::GetWiFiAuthModeEnum(record->authmode);

  auto it = _findNetwork(record->bssid);
  if (it != s_wifiNetworks.end()) {
    // Update the network
    memcpy(it->ssid, record->ssid, sizeof(it->ssid));
    it->channel     = record->primary;
    it->rssi        = record->rssi;
    it->authMode    = authMode;
    it->scansMissed = 0;

    _broadcastWifiNetworkUpdated(*it);
    ESP_LOGV(TAG, "Updated network %s (" BSSID_FMT ") with new scan info", it->ssid, BSSID_ARG(it->bssid));

    return;
  }

  WiFiNetwork network(record->ssid, record->bssid, record->primary, record->rssi, authMode, 0);

  Config::WiFiCredentials creds;
  if (Config::TryGetWiFiCredentialsByBSSID(record->bssid, creds)) {
    network.credentialsID = creds.id;
  }

  _broadcastWifiNetworkDiscovered(network);
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

bool _authenticate(const WiFiNetwork& net, const std::string& password) {
  std::uint8_t id = Config::AddWiFiCredentials(net.ssid, net.bssid, password);
  if (id == 0) {
    _broadcastErrorMessage("too_many_credentials");
    return false;
  }

  _broadcastWifiNetworkSaved(net);

  ESP_LOGI(TAG, "Added WiFi credentials for %s", net.ssid);
  wl_status_t stat = WiFi.begin(net.ssid, password.c_str(), 0, net.bssid, true);
  if (stat != WL_CONNECTED) {
    ESP_LOGE(TAG, "Failed to connect to network %s, error code %d", net.ssid, stat);
    return false;
  }

  return true;
}

bool WiFiManager::Save(const char* ssid, const std::string& password) {
  ESP_LOGV(TAG, "Authenticating to network %s", ssid);

  auto it = _findNetwork(ssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", ssid);

    _broadcastErrorMessage("network_not_found");

    return false;
  }

  return _authenticate(*it, password);
}

bool WiFiManager::Save(const std::uint8_t (&bssid)[6], const std::string& password) {
  ESP_LOGV(TAG, "Authenticating to network " BSSID_FMT, BSSID_ARG(bssid));

  auto it = _findNetwork(bssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with BSSID " BSSID_FMT, BSSID_ARG(bssid));

    _broadcastErrorMessage("network_not_found");

    return false;
  }

  return _authenticate(*it, password);
}

bool WiFiManager::Forget(const char* ssid) {
  ESP_LOGV(TAG, "Forgetting network %s", ssid);

  auto it = _findNetwork(ssid);
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

bool _isSaved(std::function<bool(const Config::WiFiCredentials&)> predicate) {
  const auto& credentials = Config::GetWiFiCredentials();

  return std::any_of(credentials.begin(), credentials.end(), predicate);
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

bool _connect(const Config::WiFiCredentials& creds) {
  ESP_LOGV(TAG, "Connecting to network %s (" BSSID_FMT ")", creds.ssid.c_str(), BSSID_ARG(creds.bssid));

  s_wifiConnecting = true;
  if (WiFi.begin(creds.ssid.c_str(), creds.password.c_str(), 0, nullptr, true) == WL_CONNECT_FAILED) {
    s_wifiConnecting = false;
    return false;
  }

  return true;
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

  if (!s_wifiConnected) {
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

  if (!s_wifiConnected) {
    s_preferredCredentialsID = creds.id;
    return true;
  }

  return false;
}

void WiFiManager::Disconnect() {
  WiFi.disconnect(false);
}

bool WiFiManager::IsConnected() {
  return s_wifiConnected;
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
  if (s_wifiConnected || s_wifiConnecting || WiFiScanManager::IsScanning()) return;

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
  if (!GetNextWiFiNetwork(creds)) {
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
