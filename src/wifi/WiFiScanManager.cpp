#include <freertos/FreeRTOS.h>

#include "wifi/WiFiScanManager.h"

const char* const TAG = "WiFiScanManager";

#include <esp_wifi.h>

#include <cstring>
#include <vector>

#include "Logging.h"
#include "SimpleMutex.h"

const uint16_t OPENSHOCK_WIFI_SCAN_INITIAL_NETWORKS_CAPACITY = 16;
const uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL                = 13;
const uint32_t OPENSHOCK_WIFI_SCAN_DWELL_PER_CHANNEL         = 100;
const uint32_t OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL        = 100;
const uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL        = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results
const uint32_t OPENSHOCK_WIFI_SCAN_TIMEOUT_MS                = 10 * 1000;

static OpenShock::SimpleMutex s_mtx      = {};
static volatile bool s_scanning          = false;
static volatile bool s_abortRequested    = false;
static wifi_scan_config_t s_scan_config  = {};
static uint16_t s_networks_size          = 0;
static uint16_t s_networks_capacity      = 0;
static wifi_ap_record_t* s_networks_data = nullptr;

static void notify_done()
{
  s_scanning = false;
}

static void notify_failed()
{
  s_scanning = false;
}

static void notify_aborted()
{
  s_scanning = false;
}

static void event_wifi_scan_done_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_id;

  if (s_abortRequested) {
    notify_aborted();
    return;
  }

  auto data = reinterpret_cast<wifi_event_sta_scan_done_t*>(event_data);

  if (data->status == 1) {
    OS_LOGE(TAG, "WiFi Scan failed!");
    notify_failed();
    return;
  }

  OS_LOGD(TAG, "WiFi scan completed, results: %hhu scan id: %hhu", data->number, data->scan_id);

  esp_err_t err;

  uint16_t numRecords;
  err = esp_wifi_scan_get_ap_num(&numRecords);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to get scan result count: %d", err);
    notify_failed();
    return;
  }

  OS_LOGI(TAG, "Values: %hhu <-> %hu", data->number, numRecords);

  if (s_networks_data == nullptr) {
    OS_LOGD(TAG, "Initializing scan results at %hhu to %hhu elements", s_scan_config.channel, numRecords);
    s_networks_size     = 0;
    s_networks_capacity = numRecords + OPENSHOCK_WIFI_SCAN_INITIAL_NETWORKS_CAPACITY;
    s_networks_data     = new wifi_ap_record_t[s_networks_capacity];
  } else if (s_networks_capacity < s_networks_size + numRecords) {
    OS_LOGD(TAG, "Resizing scan results at %hhu from %hhu to %hhu elements", s_scan_config.channel, numRecords);
    uint16_t newCapacity      = s_networks_size + numRecords;
    wifi_ap_record_t* newData = new wifi_ap_record_t[newCapacity];

    memcpy(newData, s_networks_data, s_networks_size);

    delete[] s_networks_data;

    s_networks_capacity = newCapacity;
    s_networks_data     = newData;
  }

  err = esp_wifi_scan_get_ap_records(&numRecords, s_networks_data + s_networks_size);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to get scan results: %d", err);
    notify_failed();
    return;
  }
  s_networks_size += numRecords;

  if (--s_scan_config.channel <= 0) {
    notify_done();
    return;
  }

  err = esp_wifi_scan_start(&s_scan_config, /* block: */ false);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to start next scan: %d", err);
    notify_failed();
    return;
  }
}

bool OpenShock::WiFiScanManager::Init()
{
  // Initializtion guard
  static bool initialized = false;
  if (initialized) return true;
  initialized = true;

  esp_err_t err;

  s_scanning       = false;
  s_abortRequested = false;
  memset(&s_scan_config, 0, sizeof(wifi_scan_config_t));

  err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, event_wifi_scan_done_handler, nullptr);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to register scan done handler!");
    return false;
  }

  return true;
}

bool OpenShock::WiFiScanManager::IsScanning()
{
  return s_scanning;
}

bool OpenShock::WiFiScanManager::StartScan()
{
  OpenShock::ScopedLock lock__(s_mtx);
  if (!lock__.isLocked()) {
    return false;
  }

  if (s_scanning) {
    return true;
  }

  s_scanning       = true;
  s_abortRequested = false;

  memset(&s_scan_config, 0, sizeof(wifi_scan_config_t));
  s_scan_config.channel              = OPENSHOCK_WIFI_SCAN_MAX_CHANNEL;
  s_scan_config.show_hidden          = true;
  s_scan_config.scan_type            = WIFI_SCAN_TYPE_ACTIVE;
  s_scan_config.scan_time.active.min = OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL;
  s_scan_config.scan_time.active.max = OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL;
  s_scan_config.home_chan_dwell_time = OPENSHOCK_WIFI_SCAN_DWELL_PER_CHANNEL;

  s_networks_size = 0;

  esp_err_t err = esp_wifi_scan_start(&s_scan_config, /* block: */ false);
  if (err != ERR_OK) {
    s_scanning = false;
    OS_LOGE(TAG, "Failed to start scan: %d", err);
    return false;
  }

  return true;
}

void OpenShock::WiFiScanManager::AbortScan()
{
  s_abortRequested = true;
}

bool OpenShock::WiFiScanManager::FreeResources()
{
  OpenShock::ScopedLock lock__(s_mtx);
  if (!lock__.isLocked()) {
    return false;
  }

  if (s_scanning) {
    return false;
  }

  s_networks_size     = 0;
  s_networks_capacity = 0;

  delete[] s_networks_data;
  s_networks_data = nullptr;

  return true;
}
