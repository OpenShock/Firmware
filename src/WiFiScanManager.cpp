#include "WiFiScanManager.h"

#include <WiFi.h>

#include <esp_log.h>

#include <unordered_map>

const char* const TAG = "WiFiScanManager";

constexpr const std::uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL         = 13;
constexpr const std::uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results

using namespace OpenShock;

static bool s_initialized            = false;
static bool s_scanInProgress         = false;
static bool s_channelScanDone        = false;
static bool s_scanCancelled          = false;
static std::uint8_t s_currentChannel = 0;
static std::unordered_map<std::uint64_t, WiFiScanManager::ScanStartedHandler> s_scanStartedHandlers;
static std::unordered_map<std::uint64_t, WiFiScanManager::ScanCompletedHandler> s_scanCompletedHandlers;
static std::unordered_map<std::uint64_t, WiFiScanManager::ScanDiscoveryHandler> s_scanDiscoveryHandlers;

void _setScanInProgress(bool inProgress) {
  if (s_scanInProgress != inProgress) {
    s_scanInProgress = inProgress;
    if (inProgress) {
      for (auto& it : s_scanStartedHandlers) {
        it.second();
      }
      WiFi.scanDelete();
    } else {
      ScanCompletedStatus status;
      if (s_scanCancelled) {
        status          = ScanCompletedStatus::Cancelled;
        s_scanCancelled = false;
      } else {
        status = ScanCompletedStatus::Completed;
      }
      for (auto& it : s_scanCompletedHandlers) {
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
    for (auto& it : s_scanCompletedHandlers) {
      it.second(ScanCompletedStatus::Error);
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

    for (auto& it : s_scanDiscoveryHandlers) {
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
void WiFiScanManager::CancelScan() {
  if (!s_scanInProgress) {
    ESP_LOGW(TAG, "Cannot cancel scan: no scan is in progress");
    return;
  }

  s_scanCancelled  = true;
  s_currentChannel = 0;
}

std::uint64_t WiFiScanManager::RegisterScanStartedHandler(const WiFiScanManager::ScanStartedHandler& handler) {
  static std::uint64_t nextHandle = 0;
  std::uint64_t handle            = nextHandle++;
  s_scanStartedHandlers[handle]   = handler;
  return handle;
}
void WiFiScanManager::UnregisterScanStartedHandler(std::uint64_t handle) {
  auto it = s_scanStartedHandlers.find(handle);

  if (it != s_scanStartedHandlers.end()) {
    s_scanStartedHandlers.erase(it);
  }
}

std::uint64_t WiFiScanManager::RegisterScanCompletedHandler(const WiFiScanManager::ScanCompletedHandler& handler) {
  static std::uint64_t nextHandle = 0;
  std::uint64_t handle            = nextHandle++;
  s_scanCompletedHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterScanCompletedHandler(std::uint64_t handle) {
  auto it = s_scanCompletedHandlers.find(handle);

  if (it != s_scanCompletedHandlers.end()) {
    s_scanCompletedHandlers.erase(it);
  }
}

std::uint64_t WiFiScanManager::RegisterScanDiscoveryHandler(const WiFiScanManager::ScanDiscoveryHandler& handler) {
  static std::uint64_t nextHandle = 0;
  std::uint64_t handle            = nextHandle++;
  s_scanDiscoveryHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterScanDiscoveryHandler(std::uint64_t handle) {
  auto it = s_scanDiscoveryHandlers.find(handle);

  if (it != s_scanDiscoveryHandlers.end()) {
    s_scanDiscoveryHandlers.erase(it);
  }
}

void WiFiScanManager::Update() {
  if (!s_initialized) return;

  if (s_scanInProgress && s_channelScanDone) {
    _iterateChannel();
  }
}
