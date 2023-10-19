#include "WiFiScanManager.h"

#include "Logging.h"

#include <WiFi.h>

#include <map>

const char* const TAG = "WiFiScanManager";

constexpr const std::uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL         = 13;
constexpr const std::uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results

using namespace OpenShock;

static bool s_initialized            = false;
static bool s_scanInProgress         = false;
static bool s_channelScanDone        = false;
static bool s_scanAborted            = false;
static std::uint8_t s_currentChannel = 0;
static std::map<std::uint64_t, WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::map<std::uint64_t, WiFiScanManager::NetworkDiscoveryHandler> s_networkDiscoveryHandlers;

void _setScanInProgress(bool inProgress) {
  if (s_scanInProgress != inProgress) {
    s_scanInProgress = inProgress;
    if (inProgress) {
      for (auto& it : s_statusChangedHandlers) {
        it.second(WifiScanStatus::Started);
        it.second(WifiScanStatus::InProgress);
      }
      WiFi.scanDelete();
    } else {
      WifiScanStatus status;
      if (s_scanAborted) {
        status        = WifiScanStatus::Aborted;
        s_scanAborted = false;
      } else {
        status = WifiScanStatus::Completed;
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
      it.second(WifiScanStatus::Error);
    }
    return;
  }

  ESP_LOGE(TAG, "Scan returned an unknown error");
}

void _iterateChannel() {
  if (s_currentChannel-- <= 1) {
    s_currentChannel = 0;
    _setScanInProgress(false);
    return;
  }

  s_channelScanDone = false;

  std::int16_t retval = WiFi.scanNetworks(true, true, false, OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL, s_currentChannel);

  if (retval == WIFI_SCAN_RUNNING) {
    _setScanInProgress(true);
    return;
  }

  _handleScanError(retval);
}

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info) {
  std::uint16_t numNetworks = WiFi.scanComplete();
  if (numNetworks < 0) {
    _handleScanError(numNetworks);
    return;
  }

  for (std::uint16_t i = 0; i < numNetworks; i++) {
    wifi_ap_record_t* record = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    if (record == nullptr) {
      ESP_LOGE(TAG, "Failed to get scan info for network #%u", i);
      return;
    }

    for (auto& it : s_networkDiscoveryHandlers) {
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

  WiFi.enableSTA(true);
  s_currentChannel = OPENSHOCK_WIFI_SCAN_MAX_CHANNEL;
  _iterateChannel();

  return true;
}
void WiFiScanManager::AbortScan() {
  if (!s_scanInProgress) {
    ESP_LOGW(TAG, "Cannot cancel scan: no scan is in progress");
    return;
  }

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
  static std::uint64_t nextHandle    = 0;
  std::uint64_t handle               = nextHandle++;
  s_networkDiscoveryHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterNetworkDiscoveryHandler(std::uint64_t handle) {
  auto it = s_networkDiscoveryHandlers.find(handle);

  if (it != s_networkDiscoveryHandlers.end()) {
    s_networkDiscoveryHandlers.erase(it);
  }
}

void WiFiScanManager::Update() {
  if (!s_initialized) return;

  if (s_scanInProgress && s_channelScanDone) {
    _iterateChannel();
  }
}
