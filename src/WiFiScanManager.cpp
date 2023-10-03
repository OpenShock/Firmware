#include "WiFiScanManager.h"

#include "CaptivePortal.h"

#include <ArduinoJson.h>

#include <WiFi.h>

#include <unordered_map>

const char* const TAG = "WiFiScanManager";

constexpr const std::size_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 150;

using namespace OpenShock;

enum class CurrentScanStatus {
  Idle,
  Starting,
  Running,
  Complete,
  Cancelled,
  Error,
};

static bool s_initialized = false;
static bool s_scanInProgress;
static std::uint8_t s_currentChannel;
static CurrentScanStatus s_currentScanStatus;
static std::unordered_map<WiFiScanManager::CallbackHandle, WiFiScanManager::ScanStartedHandler> s_scanStartedHandlers;
static std::unordered_map<WiFiScanManager::CallbackHandle, WiFiScanManager::ScanCompletedHandler> s_scanCompletedHandlers;
static std::unordered_map<WiFiScanManager::CallbackHandle, WiFiScanManager::ScanDiscoveryHandler> s_scanDiscoveryHandlers;

void _scanAllChannels();
void _scanCurrentChannel();
void _iterateChannel();
void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info);
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info);

bool WiFiScanManager::Init() {
  if (s_initialized) {
    ESP_LOGE(TAG, "WiFiScanManager::Init() called twice");
    return false;
  }

  s_currentChannel    = 0;
  s_scanInProgress    = false;
  s_currentScanStatus = CurrentScanStatus::Idle;
  s_scanStartedHandlers.clear();
  s_scanCompletedHandlers.clear();
  s_scanDiscoveryHandlers.clear();

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evSTAStopped, ARDUINO_EVENT_WIFI_STA_STOP);
  WiFi.enableSTA(true);

  _scanAllChannels();

  s_initialized = true;

  return true;
}

bool WiFiScanManager::StartScan() {
  if (s_currentChannel != 0) {
    ESP_LOGE(TAG, "Cannot start scan: scan is already in progress");
    return false;
  }

  WiFi.enableSTA(true);
  _iterateChannel();

  for (auto& it : s_scanStartedHandlers) {
    it.second();
  }

  return true;
}
void WiFiScanManager::CancelScan() {
  if (s_currentChannel == 0) {
    ESP_LOGE(TAG, "Cannot cancel scan: no scan is in progress");
    return;
  }

  WiFi.scanDelete();
  s_currentChannel = 0;

  for (auto& it : s_scanCompletedHandlers) {
    it.second(WiFiScanManager::ScanCompletedStatus::Cancelled);
  }
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

  if (s_currentScanStatus == CurrentScanStatus::Starting) {
    s_currentChannel = 0;
    _iterateChannel();
  } else if (s_currentScanStatus == CurrentScanStatus::Complete) {
    _iterateChannel();
  }
}

void _handleScanFinished() {
  s_currentChannel    = 0;
  s_scanInProgress    = false;
  s_currentScanStatus = CurrentScanStatus::Idle;
  ESP_LOGD(TAG, "Scan finished");
  for (auto& it : s_scanCompletedHandlers) {
    it.second(WiFiScanManager::ScanCompletedStatus::Success);
  }
}
void _handleScanError(std::int16_t retval) {
  if (retval >= 0) return;  // This isn't an error

  if (retval == WIFI_SCAN_RUNNING) {
    ESP_LOGE(TAG, "Scan is still running");
    return;
  }

  s_currentScanStatus = CurrentScanStatus::Error;
  if (retval == WIFI_SCAN_FAILED) {
    ESP_LOGE(TAG, "Failed to start scan on channel %u", s_currentChannel);
    CaptivePortal::Start();
    for (auto& it : s_scanCompletedHandlers) {
      it.second(WiFiScanManager::ScanCompletedStatus::Error);
    }
    return;
  }

  ESP_LOGE(TAG, "Scan returned an unknown error");
}
void _scanAllChannels() {
  s_currentChannel = 0;
  ESP_LOGD(TAG, "Scanning entire WiFi spectrum...");
  WiFi.scanNetworks(true, true, false, OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL);
}
void _scanCurrentChannel() {
  std::int16_t retval = WiFi.scanNetworks(true, true, false, OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL, s_currentChannel);
  s_scanInProgress    = retval == WIFI_SCAN_RUNNING;
  if (s_scanInProgress) {
    s_currentScanStatus = CurrentScanStatus::Running;
    ESP_LOGD(TAG, "Scanning channel %u", s_currentChannel);
    return;
  }
  if (retval >= 0) {
    s_currentScanStatus = CurrentScanStatus::Complete;
    return;
  }

  _handleScanError(retval);
}
void _iterateChannel() {
  if (s_currentChannel >= 14) {
    _handleScanFinished();
    return;
  }

  s_currentChannel++;
  _scanCurrentChannel();
}
void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info) {
  std::uint16_t numNetworks = WiFi.scanComplete();
  if (numNetworks < 0) {
    _handleScanError(numNetworks);
    return;
  }

  ESP_LOGD(TAG, "Scan on channel %u complete, found %u networks", s_currentChannel, numNetworks);
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
}
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info) {
  // TODO: CLEAR RESULTS
}
