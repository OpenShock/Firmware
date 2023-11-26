#include "wifi/WiFiScanManager.h"

#include "Logging.h"
#include "Time.h"

#include <WiFi.h>

#include <map>

const char* const TAG = "WiFiScanManager";

constexpr const std::uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL         = 13;
constexpr const std::uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results
constexpr const std::uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_SCAN    = 20'000;

using namespace OpenShock;

static bool s_initialized                      = false;
static bool s_scanInProgress                   = false;
static bool s_channelScanDone                  = false;
static bool s_scanAborted                      = false;
static int64_t s_lastChannelScanStartTimestamp = 0;
static int64_t s_lastScanStartTimestamp        = 0;
static std::uint8_t s_currentChannel           = 0;
static std::map<std::uint64_t, WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::map<std::uint64_t, WiFiScanManager::NetworkDiscoveryHandler> s_networkDiscoveredHandlers;

void _setScanInProgress(bool inProgress) {
  if (s_scanInProgress != inProgress) {
    s_scanInProgress = inProgress;
    if (inProgress) {
      for (auto& it : s_statusChangedHandlers) {
        it.second(WiFiScanStatus::Started);
        it.second(WiFiScanStatus::InProgress);
      }
      WiFi.scanDelete();
    } else {
      WiFiScanStatus status;
      if (s_scanAborted) {
        status        = WiFiScanStatus::Aborted;
        s_scanAborted = false;
      } else {
        status = WiFiScanStatus::Completed;
      }
      for (auto& it : s_statusChangedHandlers) {
        it.second(status);
      }
    }
  }

  if (!inProgress) {
    s_currentChannel  = 0;
    s_channelScanDone = false;
  }
}

void _handleScanError(std::int16_t retval) {
  s_channelScanDone = true;

  if (retval == WIFI_SCAN_FAILED) {
    ESP_LOGE(TAG, "Failed to start scan on channel %u", s_currentChannel);
    for (auto& it : s_statusChangedHandlers) {
      it.second(WiFiScanStatus::Error);
    }
    return;
  }

  ESP_LOGE(TAG, "Scan returned an unknown error");
}

void _iterateChannel() {
  s_currentChannel++;
  if (s_currentChannel > OPENSHOCK_WIFI_SCAN_MAX_CHANNEL) {
    s_currentChannel = 0;
    _setScanInProgress(false);
    return;
  }

  s_lastChannelScanStartTimestamp = OpenShock::millis();
  s_channelScanDone               = false;

  if (s_currentChannel > 0) {
    std::int16_t retval = WiFi.scanNetworks(true, true, false, OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL, s_currentChannel);

    if (retval == WIFI_SCAN_RUNNING) {
      _setScanInProgress(true);
      return;
    }

    _handleScanError(retval);
  }
}

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info) {
  std::int16_t numNetworks = WiFi.scanComplete();
  if (numNetworks < 0) {
    _handleScanError(numNetworks);
    return;
  }

  for (std::int16_t i = 0; i < numNetworks; i++) {
    wifi_ap_record_t* record = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    if (record == nullptr) {
      ESP_LOGE(TAG, "Failed to get scan info for network #%d", i);
      return;
    }

    for (auto& it : s_networkDiscoveredHandlers) {
      it.second(record);
    }
  }

  s_channelScanDone = true;
}
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info) {
  _setScanInProgress(false);
}

bool WiFiScanManager::Init() {
  if (s_initialized) {
    ESP_LOGW(TAG, "WiFiScanManager::Init() called twice");
    return false;
  }

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evSTAStopped, ARDUINO_EVENT_WIFI_STA_STOP);

  s_initialized = true;

  return true;
}

bool WiFiScanManager::IsScanning() {
  return s_scanInProgress;
}

bool WiFiScanManager::StartScan() {
  if (s_scanInProgress) {
    ESP_LOGW(TAG, "Cannot start scan: scan is already in progress");
    return false;
  }

  // If not already connected to a network, disable STA to fix stuck scans
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.enableSTA(false);
  }

  s_lastScanStartTimestamp = OpenShock::millis();
  s_currentChannel         = 0;
  s_channelScanDone        = false;
  _iterateChannel();

  return true;
}
void WiFiScanManager::AbortScan() {
  if (!s_scanInProgress) {
    ESP_LOGW(TAG, "Cannot cancel scan: no scan is in progress");
    return;
  }

  _setScanInProgress(false);
  s_scanAborted    = true;
  s_currentChannel = 0;
}

std::uint64_t WiFiScanManager::RegisterStatusChangedHandler(const WiFiScanManager::StatusChangedHandler& handler) {
  static std::uint64_t nextHandle = 0;
  std::uint64_t handle            = nextHandle++;
  s_statusChangedHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterStatusChangedHandler(std::uint64_t handle) {
  auto it = s_statusChangedHandlers.find(handle);

  if (it != s_statusChangedHandlers.end()) {
    s_statusChangedHandlers.erase(it);
  }
}

std::uint64_t WiFiScanManager::RegisterNetworkDiscoveryHandler(const WiFiScanManager::NetworkDiscoveryHandler& handler) {
  static std::uint64_t nextHandle     = 0;
  std::uint64_t handle                = nextHandle++;
  s_networkDiscoveredHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterNetworkDiscoveredHandler(std::uint64_t handle) {
  auto it = s_networkDiscoveredHandlers.find(handle);

  if (it != s_networkDiscoveredHandlers.end()) {
    s_networkDiscoveredHandlers.erase(it);
  }
}

void WiFiScanManager::Update() {
  if (!s_initialized) return;

  if (s_scanInProgress && !s_channelScanDone && (OpenShock::millis() - s_lastChannelScanStartTimestamp > OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL * 2)) {
    ESP_LOGD(TAG, "Scan on channel %u timed out", s_currentChannel);
    _handleScanError(WIFI_SCAN_FAILED);
  }

  if (s_scanInProgress && (OpenShock::millis() - s_lastScanStartTimestamp > OPENSHOCK_WIFI_SCAN_MAX_MS_PER_SCAN)) {
    ESP_LOGD(TAG, "Scan timed out");
    _handleScanError(WIFI_SCAN_FAILED);
    AbortScan();
  }

  if (s_scanInProgress && s_channelScanDone) {
    ESP_LOGD(TAG, "Scan on channel %u completed", s_currentChannel);
    _iterateChannel();
  }
}
