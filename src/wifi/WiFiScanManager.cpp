#include "wifi/WiFiScanManager.h"

#include "Logging.h"

#include <WiFi.h>

#include <map>

const char* const TAG = "WiFiScanManager";

constexpr const std::uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL         = 13;
constexpr const std::uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results
constexpr const std::uint32_t OPENSHOCK_WIFI_SCAN_TIMEOUT_MS = 10 * 1000;

enum WiFiScanTaskNotificationFlags {
  CHANNEL_DONE  = 1 << 0,
  ERROR         = 1 << 1,
  WIFI_DISABLED = 1 << 2,
  CLEAR_FLAGS   = CHANNEL_DONE | ERROR
};

using namespace OpenShock;

static TaskHandle_t s_scanTaskHandle     = nullptr;
static SemaphoreHandle_t s_scanTaskMutex = xSemaphoreCreateBinary();
static std::uint8_t s_currentChannel = 0;
static std::map<std::uint64_t, WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::map<std::uint64_t, WiFiScanManager::NetworkDiscoveryHandler> s_networkDiscoveredHandlers;

bool _notifyTask(WiFiScanTaskNotificationFlags flags) {
  if (xSemaphoreTake(s_scanTaskMutex, portMAX_DELAY) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to take scan task mutex");
    return false;
  }

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

void _handleScanError(std::int16_t retval) {
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

std::int16_t _scanChannel(std::uint8_t channel) {
  std::int16_t retval = WiFi.scanNetworks(true, true, false, OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL, channel);
  if (retval == WIFI_SCAN_RUNNING || retval >= 0) {
    return retval;
  }

  _handleScanError(retval);

  return retval;
}

WiFiScanStatus _scanningTaskImpl() {
  WiFi.enableSTA(true);
  WiFi.scanDelete();

  std::uint8_t channel = OPENSHOCK_WIFI_SCAN_MAX_CHANNEL;

  std::int16_t retval = _scanChannel(channel);
  if (retval != WIFI_SCAN_RUNNING) {
    // TODO: Handle this
    return WiFiScanStatus::Error;
  }

  _notifyStatusChangedHandlers(WiFiScanStatus::Started);
  _notifyStatusChangedHandlers(WiFiScanStatus::InProgress);

  while (true) {
    std::uint32_t notificationFlags = 0;

    // Wait for the scan to complete
    if (xTaskNotifyWait(0, WiFiScanTaskNotificationFlags::CLEAR_FLAGS, &notificationFlags, pdMS_TO_TICKS(OPENSHOCK_WIFI_SCAN_TIMEOUT_MS)) != pdTRUE) {
      ESP_LOGE(TAG, "Scan timed out");
      return WiFiScanStatus::Error; // TODO: Add a "timed out" status
    }

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
    if (retval != WIFI_SCAN_RUNNING) {
      // TODO: Handle this
      return WiFiScanStatus::Error;
    }
  }

  return WiFiScanStatus::Completed;
}

void _scanningTask(void* arg) {
  WiFiScanStatus status = _scanningTaskImpl();
  _notifyStatusChangedHandlers(status);

  // Clear the task handle
  xSemaphoreTake(s_scanTaskMutex, portMAX_DELAY);
  s_scanTaskHandle = nullptr;
  xSemaphoreGive(s_scanTaskMutex);

  // Commit suicide
  vTaskDelete(nullptr);
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

  // Notify the scan task that we're done
  _notifyTask(WiFiScanTaskNotificationFlags::CHANNEL_DONE);
}
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info) {
  _notifyTask(WiFiScanTaskNotificationFlags::WIFI_DISABLED);
}

bool WiFiScanManager::Init() {
  // Initialize the scan semaphore
  if (xSemaphoreGive(s_scanTaskMutex) != pdTRUE) {
    ESP_LOGE(TAG, "Initialize function called more than once");
    return false;
  }

  WiFi.onEvent(_evScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
  WiFi.onEvent(_evSTAStopped, ARDUINO_EVENT_WIFI_STA_STOP);

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
  if (xTaskCreate(_scanningTask, "WiFiScanManager", 4096, nullptr, 1, &s_scanTaskHandle) != pdPASS) {
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
