#include <freertos/FreeRTOS.h>

#include "wifi/WiFiScanManager.h"

const char* const TAG = "WiFiScanManager";

#include "Logging.h"
#include "SimpleMutex.h"

#include <esp_wifi.h>

#include <atomic>
#include <map>
#include <vector>

// Per-channel active-scan dwell time. esp_wifi walks every channel itself, so
// this is the only knob that affects scan duration / result quality.
const uint32_t OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL = 100;
const uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;

static bool s_initialized       = false;
static std::atomic<bool> s_scanning = false;
static OpenShock::SimpleMutex s_handlersMutex = {};
static std::map<uint64_t, OpenShock::WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::map<uint64_t, OpenShock::WiFiScanManager::NetworksDiscoveredHandler> s_networksDiscoveredHandlers;

using namespace OpenShock;

static void notifyStatusChangedHandlers(WiFiScanStatus status)
{
  ScopedLock lock__(&s_handlersMutex);
  for (auto& it : s_statusChangedHandlers) {
    it.second(status);
  }
}

static void notifyNetworksDiscoveredHandlers(const std::vector<const wifi_ap_record_t*>& records)
{
  ScopedLock lock__(&s_handlersMutex);
  for (auto& it : s_networksDiscoveredHandlers) {
    it.second(records);
  }
}

// Fetch the scan results esp_wifi buffered for us and fan them out. Runs in the
// esp_event loop task (WIFI_EVENT_SCAN_DONE).
static void handleScanDone()
{
  uint16_t apCount = 0;
  esp_err_t err    = esp_wifi_scan_get_ap_num(&apCount);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(err));
    s_scanning.store(false, std::memory_order_relaxed);
    notifyStatusChangedHandlers(WiFiScanStatus::Error);
    return;
  }

  std::vector<wifi_ap_record_t> records(apCount);
  if (apCount > 0) {
    err = esp_wifi_scan_get_ap_records(&apCount, records.data());
    if (err != ESP_OK) {
      OS_LOGE(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(err));
      s_scanning.store(false, std::memory_order_relaxed);
      notifyStatusChangedHandlers(WiFiScanStatus::Error);
      return;
    }
    records.resize(apCount);
  }

  // The handlers expect stable pointers; records[] outlives every synchronous call below.
  std::vector<const wifi_ap_record_t*> recordPtrs;
  recordPtrs.reserve(records.size());
  for (const auto& record : records) {
    recordPtrs.push_back(&record);
  }

  notifyNetworksDiscoveredHandlers(recordPtrs);

  s_scanning.store(false, std::memory_order_relaxed);
  notifyStatusChangedHandlers(WiFiScanStatus::Completed);
}

static void wifiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;
  (void)data;

  switch (id) {
    case WIFI_EVENT_SCAN_DONE:
      // Ignore spurious scan-done events that we didn't initiate (or already handled).
      if (s_scanning.load(std::memory_order_relaxed)) {
        handleScanDone();
      }
      break;
    case WIFI_EVENT_STA_STOP:
      if (s_scanning.exchange(false, std::memory_order_relaxed)) {
        OS_LOGW(TAG, "STA stopped mid-scan, aborting");
        notifyStatusChangedHandlers(WiFiScanStatus::Aborted);
      }
      break;
    default:
      break;
  }
}

bool WiFiScanManager::Init()
{
  if (s_initialized) {
    OS_LOGW(TAG, "WiFiScanManager is already initialized");
    return true;
  }

  esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }

  s_initialized = true;

  return true;
}

bool WiFiScanManager::IsScanning()
{
  return s_scanning.load(std::memory_order_relaxed);
}

bool WiFiScanManager::StartScan()
{
  // Claim the scan slot; bail if one is already running.
  bool expected = false;
  if (!s_scanning.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
    OS_LOGW(TAG, "Cannot start scan: a scan is already running");
    return false;
  }

  notifyStatusChangedHandlers(WiFiScanStatus::Started);

  wifi_scan_config_t config = {};
  config.show_hidden        = true;
  config.scan_type          = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL;
  config.scan_time.active.max = OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL;

  esp_err_t err = esp_wifi_scan_start(&config, false);  // async: WIFI_EVENT_SCAN_DONE fans out results
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
    s_scanning.store(false, std::memory_order_relaxed);
    notifyStatusChangedHandlers(WiFiScanStatus::Error);
    return false;
  }

  return true;
}

bool WiFiScanManager::AbortScan()
{
  if (!s_scanning.exchange(false, std::memory_order_relaxed)) {
    OS_LOGW(TAG, "Cannot abort scan: no scan is in progress");
    return false;
  }

  esp_err_t err = esp_wifi_scan_stop();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_scan_stop failed: %s", esp_err_to_name(err));
  }

  notifyStatusChangedHandlers(WiFiScanStatus::Aborted);

  return true;
}

uint64_t WiFiScanManager::RegisterStatusChangedHandler(const WiFiScanManager::StatusChangedHandler& handler)
{
  static uint64_t nextHandle = 0;
  ScopedLock lock__(&s_handlersMutex);
  uint64_t handle                 = nextHandle++;
  s_statusChangedHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterStatusChangedHandler(uint64_t handle)
{
  ScopedLock lock__(&s_handlersMutex);
  s_statusChangedHandlers.erase(handle);
}

uint64_t WiFiScanManager::RegisterNetworksDiscoveredHandler(const WiFiScanManager::NetworksDiscoveredHandler& handler)
{
  static uint64_t nextHandle = 0;
  ScopedLock lock__(&s_handlersMutex);
  uint64_t handle                      = nextHandle++;
  s_networksDiscoveredHandlers[handle] = handler;
  return handle;
}
void WiFiScanManager::UnregisterNetworksDiscoveredHandler(uint64_t handle)
{
  ScopedLock lock__(&s_handlersMutex);
  s_networksDiscoveredHandlers.erase(handle);
}
