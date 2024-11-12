#include <freertos/FreeRTOS.h>

#include "wifi/WiFiManager.h"

const char* const TAG = "WiFiManager";

#include <freertos/queue.h>
#include <freertos/task.h>

#include <esp_wifi.h>

#include <string_view>

#include "config/Config.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "ReadWriteMutex.h"
#include "Time.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiNetwork.h"

// Temporary fix to ensure that CDC+JTAG stay on on ESP32-C3
#if CONFIG_IDF_TARGET_ESP32C3
extern "C" void phy_bbpll_en_usb(bool en);
#endif

const uint8_t OPENSHOCK_WIFI_SCAN_MAX_CHANNEL         = 13;
const uint32_t OPENSHOCK_WIFI_SCAN_DWELL_PER_CHANNEL  = 100;
const uint32_t OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL = 100;
const uint32_t OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL = 300;  // Adjusting this value will affect the scan rate, but may also affect the scan results
const uint32_t OPENSHOCK_WIFI_SCAN_TIMEOUT_MS         = 10 * 1000;

const uint32_t OPENSHOCK_DNS_MAIN     = 0x01010101;  // 1.1.1.1
const uint32_t OPENSHOCK_DNS_BACKUP   = 0x08080808;  // 8.8.8.8
const uint32_t OPENSHOCK_DNS_FALLBACK = 0x09090909;  // 9.9.9.9

enum class WiFiTaskNotification : uint8_t {
  None,
  ScanFailed,
  ScanCompleted
}

enum class WiFiState : uint8_t {
  Disconnected = 0,
  Scanning     = 1 << 0,
  Connecting   = 1 << 1,
  Connected    = 1 << 2,
};

struct ScanResult {
  uint8_t scanId;
  uint16_t size;
  uint16_t capacity;
  wifi_ap_record_t* records;
};

static QueueHandle_t s_event_queue                                = nullptr;
static esp_netif_t* s_wifi_sta                                    = nullptr;
static esp_netif_t* s_wifi_ap                                     = nullptr;
static WiFiState s_state                                          = WiFiState::Disconnected;
static uint8_t s_target_creds                                     = 0;
static uint8_t s_scan_channel_current                             = 0;
static ScanResult s_scan_channel[OPENSHOCK_WIFI_SCAN_MAX_CHANNEL] = {};

template<std::size_t N>
static bool is_zero(const uint8_t (&array)[N])
{
  for (std::size_t i = 0; i < N; i++) {
    if (array[i] != 0) {
      return false;
    }
  }

  return true;
}

template<std::size_t N>
static std::size_t try_str_copy_fixed(uint8_t (&target)[N], std::string_view from)
{
  if (from.length() <= 0 || from.length() > N - 1) {
    target[0] = 0;
    return 0;
  }

  memcpy(target, from.data(), from.length());
  target[from.length()] = 0;

  return from.length();
}

static bool configure_sta_dns()
{
  esp_err_t err;

  esp_netif_dns_info_t dns;
  dns.ip.type = ESP_IPADDR_TYPE_V4;

  dns.ip.u_addr.ip4.addr = OPENSHOCK_DNS_MAIN;

  err = esp_netif_set_dns_info(s_wifi_sta, ESP_NETIF_DNS_MAIN, &dns);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set Main DNS: %d", err);
    return false;
  }

  dns.ip.u_addr.ip4.addr = OPENSHOCK_DNS_BACKUP;

  err = esp_netif_set_dns_info(s_wifi_sta, ESP_NETIF_DNS_BACKUP, &dns);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set Backup DNS: %d", err);
    return false;
  }

  dns.ip.u_addr.ip4.addr = OPENSHOCK_DNS_FALLBACK;

  err = esp_netif_set_dns_info(s_wifi_sta, ESP_NETIF_DNS_FALLBACK, &dns);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set Fallback DNS: %d", err);
    return false;
  }

  return true;
}

static bool set_ap_enabled(bool enabled)
{
  esp_err_t err;

  wifi_mode_t wifi_mode;
  err = esp_wifi_get_mode(&wifi_mode);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to get wifi mode: %d", err);
    return false;
  }

  bool currentState = wifi_mode == WIFI_MODE_AP || wifi_mode == WIFI_MODE_APSTA;
  if (enabled == currentState) {
    return true;
  }

  err = esp_wifi_set_mode(enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set wifi mode: %d", err);
    return false;
  }

  return true;
}

static bool is_connect_ratelimited(const OpenShock::WiFiNetwork& net)
{
  if (net.lastConnectAttempt == 0) {
    return false;
  }

  int64_t now  = OpenShock::millis();
  int64_t diff = now - net.lastConnectAttempt;
  if ((net.connectAttempts > 5 && diff < 5000) || (net.connectAttempts > 10 && diff < 10'000) || (net.connectAttempts > 15 && diff < 30'000) || (net.connectAttempts > 20 && diff < 60'000)) {
    return true;
  }

  return false;
}

static std::vector<OpenShock::WiFiNetwork>::iterator find_network(std::function<bool(OpenShock::WiFiNetwork&)> predicate, bool sortByAttractivity = true)
{
  if (sortByAttractivity) {
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), _attractivityComparer);
  }
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}

static bool mark_network_as_attempted(const uint8_t (&bssid)[6])
{
  auto it = _findNetworkByBSSID(bssid);
  if (it == s_wifiNetworks.end()) {
    return false;
  }

  it->connectAttempts++;
  it->lastConnectAttempt = OpenShock::millis();

  return true;
}

static bool get_next_network(OpenShock::Config::WiFiCredentials& creds)
{
  return find_network([&creds](const OpenShock::WiFiNetwork& net) {
    if (net.credentialsID == 0) {
      return false;
    }

    if (is_connect_ratelimited(net)) {
      return false;
    }

    if (!OpenShock::Config::TryGetWiFiCredentialsByID(net.credentialsID, creds)) {
      return false;
    }

    return true;
  }) != s_wifiNetworks.end();
}

static bool wifi_scan_start(uint8_t channel = OPENSHOCK_WIFI_SCAN_MAX_CHANNEL)
{
  wifi_scan_config_t scan_cfg = {
    .ssid                 = nullptr,
    .bssid                = nullptr,
    .channel              = channel,
    .show_hidden          = true,
    .scan_type            = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time = {
      .active = {
        .min = OPENSHOCK_WIFI_SCAN_MIN_MS_PER_CHANNEL,
        .max = OPENSHOCK_WIFI_SCAN_MAX_MS_PER_CHANNEL,
      },
    },
    .home_chan_dwell_time = OPENSHOCK_WIFI_SCAN_DWELL_PER_CHANNEL,
  };

  uint8_t lastChannel    = s_scan_channel_current;
  s_scan_channel_current = channel;

  esp_err_t err = esp_wifi_scan_start(&scan_cfg, /* block: */ false);
  if (err != ERR_OK) {
    // Restore previous variable ASAP
    s_scan_channel_current = lastChannel;

    OS_LOGE(TAG, "Failed to start scan: %d", err);

    return false;
  }

  return true;
}

static bool wifi_start_connect(std::string_view ssid, std::string_view password, uint8_t (&bssid)[6])
{
  esp_err_t err;

  if (s_state == WiFiState::Connected) {
    err = esp_wifi_disconnect();
    if (err != ERR_OK) {
      OS_LOGE(TAG, "Failed to disconnect: %d", err);
      return false;
    }

    s_state = WiFiState::Disconnected;
  }

  OS_LOGV(TAG, "Connecting to network %s (" BSSID_FMT ")", ssid, BSSID_ARG(bssid));

  if (!mark_network_as_attempted(bssid)) {
    OS_LOGE(TAG, "Network not recognized!");
    return false;
  }

  // Create WiFi config
  wifi_config_t wifi_cfg;
  memset(&wifi_cfg, 0, sizeof(wifi_config_t));
  wifi_cfg.sta.scan_method    = WIFI_ALL_CHANNEL_SCAN;      // or WIFI_FAST_SCAN to stop at ssid match
  wifi_cfg.sta.sort_method    = WIFI_CONNECT_AP_BY_SIGNAL;  // or WIFI_CONNECT_AP_BY_SECURITY
  wifi_cfg.sta.threshold.rssi = -127;
  if (try_str_copy_fixed(wifi_cfg.sta.ssid, ssid) > 0) {
    if (try_str_copy_fixed(wifi_cfg.sta.password, password) > 0) {
      wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WEP;
    }
    if (!is_zero(bssid)) {
      wifi_cfg.sta.bssid_set = 1;
      memcpy(wifi_cfg.sta.bssid, bssid, 6);
    }
  }

  // Set config before connnect
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set config: %d", err);
    return false;
  }

  s_state = WiFiState::Connecting;

  err = esp_wifi_connect();
  if (err != ERR_OK) {
    s_state = WiFiState::Disconnected;
    OS_LOGE(TAG, "Failed to stat wifi conhuhnect: %d", err);
    return false;
  }

  return true;
}

static bool wifi_connect_target_or_next()
{
  OpenShock::Config::WiFiCredentials creds;
  if (s_target_creds != 0) {
    bool foundCreds = OpenShock::Config::TryGetWiFiCredentialsByID(s_target_creds, creds);

    s_target_creds = 0;

    if (!foundCreds) {
      OS_LOGE(TAG, "Failed to find credentials with ID %u", s_target_creds);
      return false;
    }

    if (wifi_start_connect(creds.ssid, creds.password, creds.)) {
      return true;
    }

    OS_LOGE(TAG, "Failed to connect to network %s", creds.ssid.c_str());
  }

  if (!get_next_network(creds)) {
    return false;
  }

  return wifi_start_connect(creds.ssid, creds.password, nullptr);
}

static void task_handle_scan_failed()
{
  // TODO: Wtf to do here?
}

static void task_handle_scan_completed()
{
}

static void task_handle_disconnected()
{
  static int64_t lastScanRequest = 0;
  if (wifi_connect_target_or_next()) return;

  int64_t now = OpenShock::millis();

  if (lastScanRequest == 0 || now - lastScanRequest > 30'000) {
    lastScanRequest = now;

    OS_LOGV(TAG, "No networks to connect to, starting scan...");
    wifi_scan_start();
  }
}

static void task_main(void*)
{
  bool isScanning = false;
  while (true) {
    WiFiTaskNotification eventType;
    if (xQueueReceive(s_event_queue, &eventType, portMAX_DELAY) == pdTRUE) {
      switch (eventType) {
        case WiFiTaskNotification::ScanFailed:
          task_handle_scan_failed();
          break;
        case WiFiTaskNotification::ScanCompleted:
          task_handle_scan_completed();
          break;
        default:
          break;
      }
    }
  }
}

static void event_notify_task(WiFiTaskNotification notification)
{
  if (xQueueSend(s_event_queue, &notification, portMAX_DELAY) != pdTRUE) {
    OS_PANIC(TAG, "Unable to queue wifi event!");
  }
}

static void event_wifi_ready_handler(void* event_data)
{
  OS_LOGV(TAG, "WiFi Ready!");
}

static void event_wifi_scan_done_handler(void* event_data)
{
  auto data = reinterpret_cast<wifi_event_sta_scan_done_t*>(event_data);

  if (data->status == 1) {
    OS_LOGE(TAG, "WiFi Scan failed!");
    event_notify_task(WiFiTaskNotification::ScanFailed);
    return;
  }

  if (s_scan_channel_current <= 0 || s_scan_channel_current > OPENSHOCK_WIFI_SCAN_MAX_CHANNEL) {
    OS_LOGE(TAG, "WiFi Scanned channel is invalid!");
    event_notify_task(WiFiTaskNotification::ScanFailed);
    return;
  }

  OS_LOGD(TAG, "WiFi scan completed, results: %hhu scan id: %hhu", data->number, data->scan_id);

  ScanResult& result = s_scan_channel[s_scan_channel_current];

  esp_err_t err;

  uint16_t numRecords;
  err = esp_wifi_scan_get_ap_num(&numRecords);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to get scan result count: %d", err);
    event_notify_task(WiFiTaskNotification::ScanFailed);
    return;
  }

  OS_LOGI(TAG, "Values: %hhu <-> %hu", data->number, numRecords);

  result.scanId = data->scan_id;
  result.size   = numRecords;

  if (result.records == nullptr || result.capacity < numRecords) {
    OS_LOGD(TAG, "Initializing scan results at %hhu to %hhu elements", s_scan_channel_current, numRecords);
    result.capacity = numRecords;
    result.records  = reinterpret_cast<wifi_ap_record_t*>(malloc(numRecords * sizeof(wifi_ap_record_t)));
  } else if (result.capacity < numRecords) {
    OS_LOGD(TAG, "Resizing scan results at %hhu from %hhu to %hhu elements", s_scan_channel_current, numRecords);
    free(result.records);
    result.capacity = numRecords;
    result.records  = reinterpret_cast<wifi_ap_record_t*>(malloc(numRecords * sizeof(wifi_ap_record_t)));
  }

  err = esp_wifi_scan_get_ap_records(&result.size, result.records);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to get scan results: %d", err);
    event_notify_task(WiFiTaskNotification::ScanFailed);
    return;
  }

  if (--s_scan_channel_current == 0) {
    event_notify_task(WiFiTaskNotification::ScanCompleted);
    return;
  }

  wifi_scan_start(s_scan_channel_current);
}

static void event_wifi_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;

  switch (event_id) {
    case WIFI_EVENT_WIFI_READY:
      event_wifi_ready_handler(event_data);
      break;
    case WIFI_EVENT_SCAN_DONE:
      event_wifi_scan_done_handler(event_data);
      break;
    case WIFI_EVENT_STA_START:
      break;
    case WIFI_EVENT_STA_STOP:
      break;
    case WIFI_EVENT_STA_CONNECTED:
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      break;
    case WIFI_EVENT_AP_START:
      break;
    case WIFI_EVENT_AP_STOP:
      break;
    case WIFI_EVENT_AP_STACONNECTED:
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      break;
    default:
      OS_LOGW(TAG, "Received unknown event with ID: %i", event_id);
      break;
  }
}

bool OpenShock::WiFiManager::Init()
{
  // Initializtion guard
  static bool initialized = false;
  if (initialized) return true;
  initialized = true;

  esp_err_t err;

  s_event_queue = xQueueCreate(32, sizeof(WiFiTaskNotification));
  if (s_event_queue == nullptr) {
    OS_PANIC(TAG, "Failed to allocate event queue, probably out of memory!");
  }

  memset(s_scan_channel, 0, sizeof(ScanResult) * OPENSHOCK_WIFI_SCAN_MAX_CHANNEL);

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_wifi_handler, nullptr);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to subscribe to WiFi events: %d", err);
    return false;
  }

  // Get saved config
  std::string hostname;
  if (!OpenShock::Config::GetWiFiHostname(hostname)) {
    OS_LOGE(TAG, "Failed to get WiFi hostname!");
    return false;
  }

  bool anyCreds = OpenShock::Config::AnyWiFiCredentials();

  // Create default wifi initialization config
  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();

  // Configure dynamic buffer sizes
  init_cfg.static_tx_buf_num  = 0;
  init_cfg.dynamic_tx_buf_num = 32;
  init_cfg.tx_buf_type        = 1;
  init_cfg.cache_tx_buf_num   = 4;  // can't be zero!
  init_cfg.static_rx_buf_num  = 4;
  init_cfg.dynamic_rx_buf_num = 32;

  // Initialize the WiFi driver
  err = esp_wifi_init(&init_cfg);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to initialize WiFi stack: %d", err);
    return false;
  }

  // Create WiFi interface objects
  s_wifi_sta = esp_netif_create_default_wifi_sta();
  s_wifi_ap  = esp_netif_create_default_wifi_ap();

// Temporary fix to ensure that CDC+JTAG stay on on ESP32-C3
#if CONFIG_IDF_TARGET_ESP32C3
  phy_bbpll_en_usb(true);
#endif

  // Set hostname for station interface
  err = esp_netif_set_hostname(s_wifi_sta, hostname.c_str());
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set hostname for station interface: %d", err);
    return false;
  }

  // Set hostname for accesspoint interface
  err = esp_netif_set_hostname(s_wifi_ap, hostname.c_str());
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set hostname for accesspoint interface: %d", err);
    return false;
  }

  // Set wifi mode (STA / STA+AP)
  err = esp_wifi_set_mode(anyCreds ? WIFI_MODE_STA : WIFI_MODE_APSTA);
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to set mode: %d", err);
    return false;
  }

  // Start wifi
  err = esp_wifi_start();
  if (err != ERR_OK) {
    OS_LOGE(TAG, "Failed to start wifi: %d", err);
    return false;
  }

  if (OpenShock::TaskUtils::TaskCreateUniversal(wifi_task, TAG, 2048, nullptr, 5, nullptr, 1) != pdPASS) {  // TODO: Profile me
    OS_LOGE(TAG, "Failed to create WiFiManager update task");
    return false;
  }

  // Start connecting to WiFi early if we recognize the one that is cached in flash
  wifi_config_t current_conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &current_conf) == ESP_OK) {
    if (current_conf.sta.ssid[0] != '\0') {
      if (OpenShock::Config::GetWiFiCredentialsIDbySSID(reinterpret_cast<const char*>(current_conf.sta.ssid)) != 0) {
        err = esp_wifi_connect();
        if (err != ERR_OK) {
          OS_LOGE(TAG, "Failed to start connecting to wifi: %d", err);
          return false;
        }
      }
    }
  }

  return true;
}
