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
static std::uint8_t s_currentChannel = 0;
static std::unordered_map<WiFiScanManager::CallbackHandle, WiFiScanManager::ScanStartedHandler> s_scanStartedHandlers;
static std::unordered_map<WiFiScanManager::CallbackHandle, WiFiScanManager::ScanCompletedHandler> s_scanCompletedHandlers;
static std::unordered_map<WiFiScanManager::CallbackHandle, WiFiScanManager::ScanDiscoveryHandler> s_scanDiscoveryHandlers;

void _setScanInProgress(bool inProgress) {
  if (s_scanInProgress != inProgress) {
    s_scanInProgress = inProgress;
    if (inProgress) {
      ESP_LOGD(TAG, "Scan started");
      for (auto& it : s_scanStartedHandlers) {
        it.second();
      }
      WiFi.scanDelete();
    } else {
      ESP_LOGD(TAG, "Scan completed");
      for (auto& it : s_scanCompletedHandlers) {
        it.second(WiFiScanManager::ScanCompletedStatus::Success);
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
      it.second(WiFiScanManager::ScanCompletedStatus::Error);
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

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info);
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info);

bool WiFiScanManager::Init() {
  if (s_initialized) {
    ESP_LOGE(TAG, "WiFiScanManager::Init() called twice");
    return false;
  }

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evSTAStopped, ARDUINO_EVENT_WIFI_STA_STOP);

  s_initialized = true;

  return true;
}

bool WiFiScanManager::StartScan() {
  if (s_scanInProgress) {
    ESP_LOGE(TAG, "Cannot start scan: scan is already in progress");
    return false;
  }

  WiFi.enableSTA(true);
  s_currentChannel = OPENSHOCK_WIFI_SCAN_MAX_CHANNEL;
  _iterateChannel();

  return true;
}
void WiFiScanManager::CancelScan() {
  if (!s_scanInProgress) {
    ESP_LOGE(TAG, "Cannot cancel scan: no scan is in progress");
    return;
  }

  s_currentChannel = 0;
}

WiFiScanManager::CallbackHandle WiFiScanManager::RegisterScanStartedHandler(const WiFiScanManager::ScanStartedHandler& handler) {
  static WiFiScanManager::CallbackHandle nextId  = 0;
  WiFiScanManager::CallbackHandle CallbackHandle = nextId++;
  s_scanStartedHandlers[CallbackHandle]          = handler;
  return CallbackHandle;
}
void WiFiScanManager::UnregisterScanStartedHandler(WiFiScanManager::CallbackHandle id) {
  auto it = s_scanStartedHandlers.find(id);
  if (it == s_scanStartedHandlers.end()) {
    ESP_LOGE(TAG, "Cannot unregister scan handler: no handler with ID %u", id);
    return;
  }

  s_scanStartedHandlers.erase(it);
}

WiFiScanManager::CallbackHandle WiFiScanManager::RegisterScanCompletedHandler(const WiFiScanManager::ScanCompletedHandler& handler) {
  static WiFiScanManager::CallbackHandle nextId  = 0;
  WiFiScanManager::CallbackHandle CallbackHandle = nextId++;
  s_scanCompletedHandlers[CallbackHandle]        = handler;
  return CallbackHandle;
}
void WiFiScanManager::UnregisterScanCompletedHandler(WiFiScanManager::CallbackHandle id) {
  auto it = s_scanCompletedHandlers.find(id);
  if (it == s_scanCompletedHandlers.end()) {
    ESP_LOGE(TAG, "Cannot unregister scan handler: no handler with ID %u", id);
    return;
  }

  s_scanCompletedHandlers.erase(it);
}

WiFiScanManager::CallbackHandle WiFiScanManager::RegisterScanDiscoveryHandler(const WiFiScanManager::ScanDiscoveryHandler& handler) {
  static WiFiScanManager::CallbackHandle nextId  = 0;
  WiFiScanManager::CallbackHandle CallbackHandle = nextId++;
  s_scanDiscoveryHandlers[CallbackHandle]        = handler;
  return CallbackHandle;
}
void WiFiScanManager::UnregisterScanDiscoveryHandler(WiFiScanManager::CallbackHandle id) {
  auto it = s_scanDiscoveryHandlers.find(id);
  if (it == s_scanDiscoveryHandlers.end()) {
    ESP_LOGE(TAG, "Cannot unregister scan handler: no handler with ID %u", id);
    return;
  }

  s_scanDiscoveryHandlers.erase(it);
}

void WiFiScanManager::Update() {
  if (!s_initialized) return;

  if (s_scanInProgress && s_channelScanDone) {
    _iterateChannel();
  }
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
  ESP_LOGD(TAG, "STA stopped");
  _setScanInProgress(false);
}
