#include "wifi/WiFiScanManager.h"

#include "Logging.h"

#include <WiFi.h>

#include <map>

const char* const TAG = "WiFiScanManager";

const uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL         = 13;
const uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results
const uint32_t OPENSHOCK_WIFI_SCAN_TIMEOUT_MS         = 10 * 1000;

enum WiFiScanTaskNotificationFlags {
  CHANNEL_DONE  = 1 << 0,
  ERROR         = 1 << 1,
  WIFI_DISABLED = 1 << 2,
  CLEAR_FLAGS   = CHANNEL_DONE | ERROR
};

using namespace OpenShock;

static bool s_initialized = false;
static TaskHandle_t s_scanTaskHandle     = nullptr;
static SemaphoreHandle_t s_scanTaskMutex = xSemaphoreCreateMutex();
static uint8_t s_currentChannel     = 0;
static std::map<uint64_t, WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::map<uint64_t, WiFiScanManager::NetworksDiscoveredHandler> s_networksDiscoveredHandlers;

bool _notifyTask(WiFiScanTaskNotificationFlags flags) {
  xSemaphoreTake(s_scanTaskMutex, portMAX_DELAY);

  bool success = false;

  if (s_scanTaskHandle != nullptr) {
    success = xTaskNotify(s_scanTaskHandle, flags, eSetBits) == pdPASS;
  }

  xSemaphoreGive(s_scanTaskMutex);

  return success;
}

void _notifyStatusChangedHandlers(OpenShock::WiFiScanStatus status) {
  for (auto& it : s_statusChangedHandlers) {
    it.second(status);
  }
}

bool _isScanError(int16_t retval) {
  return retval < 0 && retval != WIFI_SCAN_RUNNING;
}

void _handleScanError(int16_t retval) {
  if (retval >= 0) return;

  _notifyTask(WiFiScanTaskNotificationFlags::ERROR);

  if (retval == WIFI_SCAN_FAILED) {
    ESP_LOGE(TAG, "Failed to start scan on channel %u", s_currentChannel);
    return;
  }

  if (retval == WIFI_SCAN_RUNNING) {
    ESP_LOGE(TAG, "Scan is running on channel %u", s_currentChannel);
    return;
  }

  ESP_LOGE(TAG, "Scan returned an unknown error");
}

int16_t _scanChannel(uint8_t channel) {
  int16_t retval = WiFi.scanNetworks(true, true, false, OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL, channel);
  if (!_isScanError(retval)) {
    return retval;
  }

  _handleScanError(retval);

  return retval;
}

WiFiScanStatus _scanningTaskImpl() {
  // Start the scan on the highest channel and work our way down
  uint8_t channel = OPENSHOCK_WIFI_SCAN_MAX_CHANNEL;

  // Start the scan on the first channel
  int16_t retval = _scanChannel(channel);
  if (_isScanError(retval)) {
    return WiFiScanStatus::Error;
  }

  // Notify the status changed handlers that the scan has started and is in progress
  _notifyStatusChangedHandlers(WiFiScanStatus::Started);
  _notifyStatusChangedHandlers(WiFiScanStatus::InProgress);

  // Scan each channel until we're done
  while (true) {
    uint32_t notificationFlags = 0;

    // Wait for the scan to complete, _evScanCompleted will notify us when it's done
    if (xTaskNotifyWait(0, WiFiScanTaskNotificationFlags::CLEAR_FLAGS, &notificationFlags, pdMS_TO_TICKS(OPENSHOCK_WIFI_SCAN_TIMEOUT_MS)) != pdTRUE) {
      ESP_LOGE(TAG, "Scan timed out");
      return WiFiScanStatus::TimedOut;
    }

    // Check if we were notified of an error or if WiFi was disabled
    if (notificationFlags != WiFiScanTaskNotificationFlags::CHANNEL_DONE) {
      if (notificationFlags & WiFiScanTaskNotificationFlags::WIFI_DISABLED) {
        ESP_LOGE(TAG, "Scan task exiting due to being notified that WiFi was disabled");
        return WiFiScanStatus::Aborted;
      }

      if (notificationFlags & WiFiScanTaskNotificationFlags::ERROR) {
        ESP_LOGE(TAG, "Scan task exiting due to being notified of an error");
        return WiFiScanStatus::Error;
      }

      return WiFiScanStatus::Error;
    }

    // Select the next channel, or break if we're done
    if (--channel <= 0) {
      break;
    }

    // Start the scan on the next channel
    retval = _scanChannel(channel);
    if (_isScanError(retval)) {
      return WiFiScanStatus::Error;
    }
  }

  return WiFiScanStatus::Completed;
}

void _scanningTask(void* arg) {
  (void)arg;
  
  // Start the scan
  WiFiScanStatus status = _scanningTaskImpl();

  // Notify the status changed handlers of the scan result
  _notifyStatusChangedHandlers(status);

  // Clear the task handle
  xSemaphoreTake(s_scanTaskMutex, portMAX_DELAY);
  s_scanTaskHandle = nullptr;
  xSemaphoreGive(s_scanTaskMutex);

  // Kill this task
  vTaskDelete(nullptr);
}

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info) {
  (void)event;
  (void)info;

  int16_t numNetworks = WiFi.scanComplete();
  if (_isScanError(numNetworks)) {
    _handleScanError(numNetworks);
    return;
  }

  if (numNetworks == WIFI_SCAN_RUNNING) {
    ESP_LOGE(TAG, "Scan completed but scan is still running... WTF?");
    return;
  }

  std::vector<const wifi_ap_record_t*> networkRecords;
  networkRecords.reserve(numNetworks);

  for (int16_t i = 0; i < numNetworks; i++) {
    wifi_ap_record_t* record = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    if (record == nullptr) {
      ESP_LOGE(TAG, "Failed to get scan info for network #%d", i);
      return;
    }

    networkRecords.push_back(record);
  }

  // Notify the networks discovered handlers
  for (auto& it : s_networksDiscoveredHandlers) {
    it.second(networkRecords);
  }

  // Notify the scan task that we're done
  _notifyTask(WiFiScanTaskNotificationFlags::CHANNEL_DONE);
}
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info) {
  (void)event;
  (void)info;

  _notifyTask(WiFiScanTaskNotificationFlags::WIFI_DISABLED);
}

bool WiFiScanManager::Init() {
  if (s_initialized) {
    ESP_LOGW(TAG, "WiFiScanManager is already initialized");
    return true;
  }

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evSTAStopped, ARDUINO_EVENT_WIFI_STA_STOP);

  s_initialized = true;

  return true;
}

bool WiFiScanManager::IsScanning() {
  return s_scanTaskHandle != nullptr;
}

bool WiFiScanManager::StartScan() {
  xSemaphoreTake(s_scanTaskMutex, portMAX_DELAY);

  // Check if a scan is already in progress
  if (s_scanTaskHandle != nullptr && eTaskGetState(s_scanTaskHandle) != eDeleted) {
    ESP_LOGW(TAG, "Cannot start scan: scan task is already running");

    xSemaphoreGive(s_scanTaskMutex);
    return false;
  }

  // Start the scan task
  if (xTaskCreate(_scanningTask, "WiFiScanManager", 4096, nullptr, 1, &s_scanTaskHandle) != pdPASS) {  // PROFILED: 1.8KB stack usage
    ESP_LOGE(TAG, "Failed to create scan task");

    xSemaphoreGive(s_scanTaskMutex);
    return false;
  }

  xSemaphoreGive(s_scanTaskMutex);
  return true;
}
bool WiFiScanManager::AbortScan() {
  xSemaphoreTake(s_scanTaskMutex, portMAX_DELAY);

  // Check if a scan is in progress
  if (s_scanTaskHandle == nullptr || eTaskGetState(s_scanTaskHandle) == eDeleted) {
    ESP_LOGW(TAG, "Cannot abort scan: no scan is in progress");

    xSemaphoreGive(s_scanTaskMutex);
    return false;
  }

  // Kill the task
  vTaskDelete(s_scanTaskHandle);
  s_scanTaskHandle = nullptr;

  // Inform the change handlers that the scan was aborted
  for (auto& it : s_statusChangedHandlers) {
    it.second(WiFiScanStatus::Aborted);
  }

  xSemaphoreGive(s_scanTaskMutex);
  return true;
}

uint64_t WiFiScanManager::RegisterStatusChangedHandler(const WiFiScanManager::StatusChangedHandler& handler) {
  static uint64_t nextHandle = 0;
  uint64_t handle            = nextHandle++;
  s_statusChangedHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterStatusChangedHandler(uint64_t handle) {
  auto it = s_statusChangedHandlers.find(handle);

  if (it != s_statusChangedHandlers.end()) {
    s_statusChangedHandlers.erase(it);
  }
}

uint64_t WiFiScanManager::RegisterNetworksDiscoveredHandler(const WiFiScanManager::NetworksDiscoveredHandler& handler) {
  static uint64_t nextHandle      = 0;
  uint64_t handle                 = nextHandle++;
  s_networksDiscoveredHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterNetworksDiscoveredHandler(uint64_t handle) {
  auto it = s_networksDiscoveredHandlers.find(handle);

  if (it != s_networksDiscoveredHandlers.end()) {
    s_networksDiscoveredHandlers.erase(it);
  }
}
