#include <freertos/FreeRTOS.h>

#include "wifi/WiFiScanManager.h"

const char* const TAG = "WiFiScanManager";

#include "Logging.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"

#include <WiFi.h>

#include <map>

enum WiFiScanTaskNotificationFlags {
  CHANNEL_DONE  = 1 << 0,
  ERROR         = 1 << 1,
  WIFI_DISABLED = 1 << 2,
  CLEAR_FLAGS   = CHANNEL_DONE | ERROR
};

static bool s_initialized                     = false;
static TaskHandle_t s_scanTaskHandle          = nullptr;
static OpenShock::SimpleMutex s_scanTaskMutex = {};
static uint8_t s_currentChannel               = 0;
static std::map<uint64_t, OpenShock::WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::map<uint64_t, OpenShock::WiFiScanManager::NetworksDiscoveredHandler> s_networksDiscoveredHandlers;

using namespace OpenShock;

WiFiScanStatus _scanningTaskImpl()
{WiFi.scanNetworks
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
      OS_LOGE(TAG, "Scan timed out");
      return WiFiScanStatus::TimedOut;
    }

    // Check if we were notified of an error or if WiFi was disabled
    if (notificationFlags != WiFiScanTaskNotificationFlags::CHANNEL_DONE) {
      if (notificationFlags & WiFiScanTaskNotificationFlags::WIFI_DISABLED) {
        OS_LOGE(TAG, "Scan task exiting due to being notified that WiFi was disabled");
        return WiFiScanStatus::Aborted;
      }

      if (notificationFlags & WiFiScanTaskNotificationFlags::ERROR) {
        OS_LOGE(TAG, "Scan task exiting due to being notified of an error");
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

void _scanningTask(void* arg)
{
  (void)arg;

  // Start the scan
  WiFiScanStatus status = _scanningTaskImpl();

  // Notify the status changed handlers of the scan result
  _notifyStatusChangedHandlers(status);

  s_scanTaskMutex.lock(portMAX_DELAY);

  // Clear the task handle
  s_scanTaskHandle = nullptr;

  s_scanTaskMutex.unlock();

  // Kill this task
  vTaskDelete(nullptr);
}

void _evScanCompleted(arduino_event_id_t event, arduino_event_info_t info)
{
  (void)event;
  (void)info;

  int16_t numNetworks = WiFi.scanComplete();
  if (_isScanError(numNetworks)) {
    _handleScanError(numNetworks);
    return;
  }

  if (numNetworks == WIFI_SCAN_RUNNING) {
    OS_LOGE(TAG, "Scan completed but scan is still running... WTF?");
    return;
  }

  std::vector<const wifi_ap_record_t*> networkRecords;
  networkRecords.reserve(numNetworks);

  for (int16_t i = 0; i < numNetworks; i++) {
    wifi_ap_record_t* record = reinterpret_cast<wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
    if (record == nullptr) {
      OS_LOGE(TAG, "Failed to get scan info for network #%d", i);
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
void _evSTAStopped(arduino_event_id_t event, arduino_event_info_t info)
{
  (void)event;
  (void)info;

  _notifyTask(WiFiScanTaskNotificationFlags::WIFI_DISABLED);
}

bool WiFiScanManager::IsScanning()
{
  // Quick check
  if (s_scanTaskHandle == nullptr) {
    return false;
  }

  // It wasnt null, lock and perform proper check
  ScopedLock lock__(&s_scanTaskMutex);

  return s_scanTaskHandle != nullptr && eTaskGetState(s_scanTaskHandle) != eDeleted;
}

bool WiFiScanManager::StartScan()
{
  ScopedLock lock__(&s_scanTaskMutex);

  // Check if a scan is already in progress
  if (s_scanTaskHandle != nullptr && eTaskGetState(s_scanTaskHandle) != eDeleted) {
    OS_LOGW(TAG, "Cannot start scan: scan task is already running");
    return false;
  }

  // Free the TCB
  if (s_scanTaskHandle != nullptr) {
    vTaskDelete(s_scanTaskHandle);
  }

  // Start the scan task
  if (TaskUtils::TaskCreateExpensive(_scanningTask, "WiFiScanManager", 4096, nullptr, 1, &s_scanTaskHandle) != pdPASS) {  // PROFILED: 1.8KB stack usage
    OS_LOGE(TAG, "Failed to create scan task");
    return false;
  }

  return true;
}
bool WiFiScanManager::AbortScan()
{
  ScopedLock lock__(&s_scanTaskMutex);

  // Check if a scan is in progress
  if (s_scanTaskHandle == nullptr || eTaskGetState(s_scanTaskHandle) == eDeleted) {
    OS_LOGW(TAG, "Cannot abort scan: no scan is in progress");
    return false;
  }

  // Kill the task
  vTaskDelete(s_scanTaskHandle);
  s_scanTaskHandle = nullptr;

  // Inform the change handlers that the scan was aborted
  for (auto& it : s_statusChangedHandlers) {
    it.second(WiFiScanStatus::Aborted);
  }

  return true;
}