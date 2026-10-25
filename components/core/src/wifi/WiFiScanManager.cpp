#include <freertos/FreeRTOS.h>

#include "wifi/WiFiScanManager.h"

const char* const TAG = "WiFiScanManager";

#include "Logging.h"

#include <esp_wifi.h>

#include <atomic>
#include <utility>
#include <vector>

// Per-channel active-scan dwell time. esp_wifi walks every channel itself, so this is the main knob for scan
// duration. While scanning, the radio is off the AP's channel, so captive-portal clients lose frames for each dwell:
// keep it close to the IDF default (120 ms) rather than 300 ms.
const uint32_t OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL = 60;
const uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 120;

static bool s_initialized           = false;
static std::atomic<bool> s_scanning = false;
// Set on the event loop, consumed by ProcessEvents() on the worker task
static std::atomic<bool> s_scanDonePending           = false;
static std::atomic<bool> s_scanAbortedPending        = false;
static OpenShock::WiFiScanManager::WakeWorker s_wake = nullptr;
// Only written before Init() (by the task that runs it), read-only afterwards
static std::vector<OpenShock::WiFiScanManager::StatusChangedHandler> s_statusChangedHandlers;
static std::vector<OpenShock::WiFiScanManager::NetworksDiscoveredHandler> s_networksDiscoveredHandlers;

using namespace OpenShock;

static void notifyStatusChangedHandlers(WiFiScanStatus status)
{
  for (auto& handler : s_statusChangedHandlers) {
    handler(status);
  }
}

static void notifyNetworksDiscoveredHandlers(const std::vector<const wifi_ap_record_t*>& records)
{
  for (auto& handler : s_networksDiscoveredHandlers) {
    handler(records);
  }
}

// Fetch the scan results esp_wifi buffered for us and fan them out. Runs on the worker task.
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

  // Default event loop: only flag the work and wake the worker. Fetching records, the handlers' config lookups and
  // portal broadcasts would otherwise delay every WiFi and IP event behind them.
  switch (id) {
    case WIFI_EVENT_SCAN_DONE:
      s_scanDonePending.store(true, std::memory_order_relaxed);
      s_wake();
      break;
    case WIFI_EVENT_STA_STOP:
      if (s_scanning.exchange(false, std::memory_order_relaxed)) {
        OS_LOGW(TAG, "STA stopped mid-scan, aborting");
        s_scanAbortedPending.store(true, std::memory_order_relaxed);
        s_wake();
      }
      break;
    default:
      break;
  }
}

void WiFiScanManager::ProcessEvents()
{
  if (s_scanAbortedPending.exchange(false, std::memory_order_relaxed)) {
    notifyStatusChangedHandlers(WiFiScanStatus::Aborted);
  }

  if (s_scanDonePending.exchange(false, std::memory_order_relaxed)) {
    if (s_scanning.load(std::memory_order_relaxed)) {
      handleScanDone();
    } else {
      // Not ours, or aborted meanwhile: free the records esp_wifi kept for us
      esp_wifi_clear_ap_list();
    }
  }
}

bool WiFiScanManager::Init(WakeWorker wakeWorker)
{
  if (s_initialized) {
    OS_LOGW(TAG, "WiFiScanManager is already initialized");
    return true;
  }

  s_wake = wakeWorker;

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

  wifi_scan_config_t config   = {};
  config.show_hidden          = true;
  config.scan_type            = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL;
  config.scan_time.active.max = OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL;
  config.home_chan_dwell_time = WIFI_SCAN_HOME_CHANNEL_DWELL_DEFAULT_TIME;  // 0 (zero-init) is not the default

  // async: WIFI_EVENT_SCAN_DONE fans out results
  esp_err_t err = esp_wifi_scan_start(&config, false);
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

bool WiFiScanManager::RegisterStatusChangedHandler(WiFiScanManager::StatusChangedHandler handler)
{
  if (s_initialized) {
    OS_LOGE(TAG, "Scan handlers must be registered before Init()");
    return false;
  }

  s_statusChangedHandlers.push_back(std::move(handler));
  return true;
}

bool WiFiScanManager::RegisterNetworksDiscoveredHandler(WiFiScanManager::NetworksDiscoveredHandler handler)
{
  if (s_initialized) {
    OS_LOGE(TAG, "Scan handlers must be registered before Init()");
    return false;
  }

  s_networksDiscoveredHandlers.push_back(std::move(handler));
  return true;
}
