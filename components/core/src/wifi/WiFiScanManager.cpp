#include <freertos/FreeRTOS.h>

#include "wifi/WiFiScanManager.h"

const char* const TAG = "WiFiScanManager";

#include "Logging.h"
#include "visual/VisualStateManager.h"

#include <esp_wifi.h>

#include <atomic>
#include <utility>
#include <vector>

// Idle -> Scanning (StartScan) -> Idle (results handled, failure, STA stop).
// Scanning -> Stopping (AbortScan) -> Idle (the stopped scan's SCAN_DONE, or STA stop).
// esp_wifi_scan_stop posts a SCAN_DONE (see the WiFi driver guide). Stopping keeps a new scan from starting until that
// event is out of the way, so it can't be taken for the new scan's results.
enum class ScanState : uint8_t {
  Idle,
  Scanning,
  Stopping,
};

static bool s_initialized             = false;
static std::atomic<ScanState> s_state = ScanState::Idle;
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

// Ends the scan this task was handling. False if AbortScan got there first, which has already reported it.
static bool finishScan()
{
  ScanState expected = ScanState::Scanning;
  if (s_state.compare_exchange_strong(expected, ScanState::Idle)) {
    return true;
  }
  if (expected == ScanState::Stopping) {
    s_state.store(ScanState::Idle);  // Its SCAN_DONE was the one just handled; none will follow
  }
  return false;
}

// Fetch the scan results esp_wifi buffered for us and fan them out. Runs on the worker task.
static void handleScanDone()
{
  uint16_t apCount = 0;
  esp_err_t err    = esp_wifi_scan_get_ap_num(&apCount);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(err));
    esp_wifi_clear_ap_list();
    if (finishScan()) {
      notifyStatusChangedHandlers(WiFiScanStatus::Error);
    }
    return;
  }

  std::vector<wifi_ap_record_t> records(apCount);
  if (apCount > 0) {
    err = esp_wifi_scan_get_ap_records(&apCount, records.data());
    if (err != ESP_OK) {
      OS_LOGE(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(err));
      esp_wifi_clear_ap_list();
      if (finishScan()) {
        notifyStatusChangedHandlers(WiFiScanStatus::Error);
      }
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

  if (finishScan()) {
    notifyStatusChangedHandlers(WiFiScanStatus::Completed);
  }
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
    {
      // A stopped scan's results, or a scan we didn't start: free them here (cheap) and keep them from being taken
      // for a later scan's
      ScanState expected = ScanState::Stopping;
      if (s_state.compare_exchange_strong(expected, ScanState::Idle) || expected == ScanState::Idle) {
        esp_wifi_clear_ap_list();
        break;
      }
      s_scanDonePending.store(true, std::memory_order_relaxed);
      s_wake();
      break;
    }
    case WIFI_EVENT_STA_STOP:
      if (s_state.exchange(ScanState::Idle) == ScanState::Scanning) {
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
    if (s_state.load() != ScanState::Idle) {
      handleScanDone();
    } else {
      // The STA stopped meanwhile: free the records esp_wifi kept for us
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
  return s_state.load() != ScanState::Idle;
}

bool WiFiScanManager::StartScan()
{
  // Claim the scan slot; bail if one is already running or still stopping.
  ScanState expected = ScanState::Idle;
  if (!s_state.compare_exchange_strong(expected, ScanState::Scanning)) {
    OS_LOGW(TAG, "Cannot start scan: a scan is already running");
    return false;
  }

  notifyStatusChangedHandlers(WiFiScanStatus::Started);

  wifi_scan_config_t config   = {};
  config.show_hidden          = true;
  config.scan_type            = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = CONFIG_OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL;
  config.scan_time.active.max = CONFIG_OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL;
  config.home_chan_dwell_time = WIFI_SCAN_HOME_CHANNEL_DWELL_DEFAULT_TIME;  // 0 (zero-init) is not the default

  // async: WIFI_EVENT_SCAN_DONE fans out results
  esp_err_t err = esp_wifi_scan_start(&config, false);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
    finishScan();
    notifyStatusChangedHandlers(WiFiScanStatus::Error);
    return false;
  }

  VisualStateManager::SetScanningStarted();  // Cleared by the WIFI_EVENT_SCAN_DONE that ends this scan

  return true;
}

bool WiFiScanManager::AbortScan()
{
  ScanState expected = ScanState::Scanning;
  if (!s_state.compare_exchange_strong(expected, ScanState::Stopping)) {
    OS_LOGW(TAG, "Cannot abort scan: no scan is in progress");
    return false;
  }

  esp_err_t err = esp_wifi_scan_stop();
  if (err != ESP_OK) {
    // Not stopped, so no SCAN_DONE of its own: let the scan run to completion. If it finished meanwhile, its SCAN_DONE
    // already moved the state on.
    OS_LOGE(TAG, "esp_wifi_scan_stop failed: %s", esp_err_to_name(err));
    expected = ScanState::Stopping;
    s_state.compare_exchange_strong(expected, ScanState::Scanning);
    return false;
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
