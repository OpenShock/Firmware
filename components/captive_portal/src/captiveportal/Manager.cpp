#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "captiveportal/Manager.h"

const char* const TAG = "CaptivePortal";

#include "AppHooks.h"
#include "captiveportal/CaptivePortalInstance.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "FormatHelpers.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "Temporal.h"

#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include "SimpleMutex.h"
#include "util/TaskUtils.h"

#include <atomic>
#include <cstring>
#include <memory>

using namespace OpenShock;

static std::atomic<bool> s_alwaysEnabled                 = false;
static std::atomic<bool> s_forceClosed                   = false;
static std::atomic<bool> s_userDone                      = false;
static std::atomic<bool> s_closeRequested                = false;  // Backend disabled the portal; close once online
static esp_timer_handle_t s_captivePortalUpdateLoopTimer = nullptr;
static TaskHandle_t s_managerTask                        = nullptr;
static SimpleMutex s_instanceMutex;
// Serializes portal start/stop between the manager task and ForceClose (called from the OTA task).
static SimpleMutex s_lifecycleMutex;
static std::shared_ptr<CaptivePortal::CaptivePortalInstance> s_instance = nullptr;
static esp_netif_t* s_apNetif                                           = nullptr;

// The captive portal AP always serves from this fixed address; the DNS server and
// RFC8908 handler reference it too (see CaptivePortal::ApIPv4String()).
static const char* const CAPTIVE_PORTAL_AP_IP = "4.3.2.1";

// Absolute esp_timer timestamps (microseconds). 0 = not armed.
static std::atomic<int64_t> s_startupGraceExpiry = 0;                     // Don't open portal until this time passes
static std::atomic<int64_t> s_autoCloseExpiry    = 0;                     // Auto-close AP when no clients connected and device is online

static constexpr int64_t STARTUP_GRACE_PERIOD_US = 30LL * 1'000'000;      // 30 seconds
static constexpr int64_t AUTO_CLOSE_DELAY_US     = 5LL * 60 * 1'000'000;  // 5 minutes

static bool isDeviceFullyConfigured()
{
  std::vector<Config::WiFiCredentials> credentialsList;
  if (!Config::GetWiFiCredentials(credentialsList) || credentialsList.empty()) {
    return false;
  }
  return Config::HasBackendAuthToken();
}

static std::shared_ptr<CaptivePortal::CaptivePortalInstance> GetInstance()
{
  ScopedLock lock__(&s_instanceMutex);
  return s_instance;
}
static bool CreateInstance()
{
  auto instance = std::make_shared<CaptivePortal::CaptivePortalInstance>();
  if (!instance->ok()) {
    OS_LOGE(TAG, "Captive portal servers failed to start");
    return false;  // `instance` is destroyed here, nothing was published
  }

  ScopedLock lock__(&s_instanceMutex);
  s_instance = std::move(instance);
  return true;
}
// Tears the instance down on the calling task and only returns once it is fully destroyed (servers stopped,
// filesystem unmounted). Other tasks may briefly hold a copy while sending; wait for those to drop first so the
// destructor never runs on an event-loop or httpd task.
static void DestroyInstance()
{
  std::shared_ptr<CaptivePortal::CaptivePortalInstance> instance;
  {
    ScopedLock lock__(&s_instanceMutex);
    instance   = std::move(s_instance);
    s_instance = nullptr;
  }

  if (instance == nullptr) {
    return;
  }

  for (int i = 0; i < 200 && instance.use_count() > 1; ++i) {  // up to ~2 s
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (instance.use_count() > 1) {
    OS_LOGW(TAG, "Captive portal instance still referenced elsewhere, it will be destroyed by the last holder");
  }

  instance.reset();
}

static bool captiveportal_start()
{
  if (GetInstance() != nullptr) {
    OS_LOGD(TAG, "Already started");
    return true;
  }

  OS_LOGI(TAG, "Starting captive portal");

  // esp_wifi is already initialized by WiFiManager; create the AP netif once and
  // pin it to a fixed IP so the DNS/HTTP portal has a stable address.
  if (s_apNetif == nullptr) {
    s_apNetif = esp_netif_create_default_wifi_ap();
    if (s_apNetif == nullptr) {
      OS_LOGE(TAG, "Failed to create AP netif");
      return false;
    }

    esp_netif_ip_info_t ipInfo = {};
    esp_netif_str_to_ip4(CAPTIVE_PORTAL_AP_IP, &ipInfo.ip);
    esp_netif_str_to_ip4(CAPTIVE_PORTAL_AP_IP, &ipInfo.gw);
    esp_netif_str_to_ip4("255.255.255.0", &ipInfo.netmask);

    esp_netif_dhcps_stop(s_apNetif);
    esp_netif_set_ip_info(s_apNetif, &ipInfo);

    // DHCP option 114 (RFC 8910): points clients straight at the RFC 8908 captive-portal API, so modern Android/iOS
    // can open the portal without waiting for their connectivity probes to be redirected. esp_netif keeps the
    // pointer, so the string must outlive the netif.
    static char captivePortalUri[64];
    snprintf(captivePortalUri, sizeof(captivePortalUri), "http://%s/captive-portal/api", CAPTIVE_PORTAL_AP_IP);  // RFC8908Handler's API path
    esp_err_t uriErr = esp_netif_dhcps_option(s_apNetif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captivePortalUri, strlen(captivePortalUri));
    if (uriErr != ESP_OK) {
      OS_LOGW(TAG, "Failed to set DHCP captive portal URI: %s", esp_err_to_name(uriErr));
    }

    esp_netif_dhcps_start(s_apNetif);
  }

  // AP SSID = prefix + this device's STA MAC (matches the old Arduino naming).
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  wifi_config_t apConfig = {};
  snprintf(reinterpret_cast<char*>(apConfig.ap.ssid), sizeof(apConfig.ap.ssid), "%s" BSSID_FMT, CONFIG_OPENSHOCK_FW_AP_PREFIX, BSSID_ARG(mac));
  apConfig.ap.ssid_len        = strlen(reinterpret_cast<char*>(apConfig.ap.ssid));
  apConfig.ap.channel         = 1;
  apConfig.ap.authmode        = WIFI_AUTH_OPEN;
  apConfig.ap.max_connection  = 4;
  apConfig.ap.beacon_interval = 100;

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to enable AP mode: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_wifi_set_config(WIFI_IF_AP, &apConfig);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to configure AP: %s", esp_err_to_name(err));
    esp_wifi_set_mode(WIFI_MODE_STA);
    return false;
  }

  if (!CreateInstance()) {
    esp_wifi_set_mode(WIFI_MODE_STA);
    return false;
  }

  return true;
}
static void captiveportal_stop()
{
  if (GetInstance() == nullptr) {
    OS_LOGD(TAG, "Already stopped");
    return;
  }

  OS_LOGI(TAG, "Stopping captive portal");

  DestroyInstance();
  s_userDone = false;

  // Drop the AP but keep the STA connection alive.
  esp_wifi_set_mode(WIFI_MODE_STA);
}

// Runs the captive-portal state machine once. This does heavy work (starting the
// portal constructs CaptivePortalInstance, which mounts LittleFS and spins up the
// HTTP/WS/DNS servers), so it MUST run on the dedicated manager task below — never
// on the esp_timer task, whose 3.5KB stack would overflow.
static void captiveportal_tick()
{
  int64_t now = esp_timer_get_time();

  // Startup grace period: device is fully configured, wait for gateway connection
  int64_t graceExpiry = s_startupGraceExpiry.load(std::memory_order_relaxed);
  if (graceExpiry != 0) {
    if (GatewayConnectionManager::IsConnected()) {
      // Gateway connected during grace — clear grace, never open portal
      s_startupGraceExpiry.store(0, std::memory_order_relaxed);
      return;
    }
    if (now < graceExpiry) {
      // Still within grace period, don't open portal yet
      return;
    }
    // Grace expired without gateway connection — open portal normally
    s_startupGraceExpiry.store(0, std::memory_order_relaxed);
  }

  // Force-closed (OTA flashing the static filesystem); cleared again by ReleaseForceClose()
  if (s_forceClosed) {
    if (GetInstance() != nullptr) {
      OS_LOGD(TAG, "Force-closing captive portal");
      captiveportal_stop();
    }
    return;
  }

  // User completed setup, or the backend disabled the portal: close it once the device is fully online
  if ((s_userDone || s_closeRequested) && GatewayConnectionManager::IsConnected()) {
    s_closeRequested = false;
    if (GetInstance() != nullptr) {
      OS_LOGI(TAG, "Setup completed or portal disabled, closing captive portal");
      captiveportal_stop();
    }
    return;
  }

  // Auto-close: no clients connected, WiFi + gateway are up, 5 minutes elapsed
  bool running    = false;
  bool hasClients = false;
  if (auto instance = GetInstance(); instance != nullptr) {
    // Only sample it: holding the reference across captiveportal_stop() would stall DestroyInstance()
    running    = true;
    hasClients = instance->hasClients();
  }
  if (running && !s_alwaysEnabled && GatewayConnectionManager::IsConnected()) {
    if (hasClients) {
      // Clients still connected, reset timer
      s_autoCloseExpiry.store(0, std::memory_order_relaxed);
    } else {
      int64_t expiry = s_autoCloseExpiry.load(std::memory_order_relaxed);
      if (expiry == 0) {
        s_autoCloseExpiry.store(now + AUTO_CLOSE_DELAY_US, std::memory_order_relaxed);
      } else if (now >= expiry) {
        OS_LOGI(TAG, "Auto-closing captive portal AP (no clients for 5 minutes)");
        captiveportal_stop();
        return;
      }
    }
  } else {
    s_autoCloseExpiry.store(0, std::memory_order_relaxed);
  }

  // Open portal if not running and device needs setup
  if (!running) {
    bool commandHandlerOk = CommandHandler::Ok();
    bool shouldStart      = s_alwaysEnabled || !commandHandlerOk || !isDeviceFullyConfigured();
    if (shouldStart) {
      OS_LOGD(TAG, "Starting captive portal");
      captiveportal_start();
    }
  }
}

// Dedicated worker task. The 500ms timer only notifies us; all the heavy tick work
// (portal start/stop, instance construction) runs here on a generous stack.
static void captiveportal_managertask(void*)
{
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ScopedLock lock__(&s_lifecycleMutex);
    captiveportal_tick();
  }
}

// esp_timer callback (runs on the esp_timer task, 3.5KB stack). Must stay trivial —
// it only kicks the manager task, which does the real work on its own stack.
static void captiveportal_timernotify(void*)
{
  if (s_managerTask != nullptr) {
    xTaskNotifyGive(s_managerTask);
  }
}

bool CaptivePortal::Init()
{
  // Restore the persisted always-enabled setting before the hook can change it, so it survives a reboot.
  Config::CaptivePortalConfig config;
  if (Config::GetCaptivePortalConfig(config)) {
    s_alwaysEnabled = config.alwaysEnabled;
  } else {
    OS_LOGE(TAG, "Failed to get captive portal config");
  }

  AppHooks::RegisterCaptivePortal(CaptivePortal::BroadcastMessageBIN, CaptivePortal::SetAlwaysEnabled);

  // If device is already fully configured, set a startup grace period before opening portal
  if (isDeviceFullyConfigured()) {
    s_startupGraceExpiry.store(esp_timer_get_time() + STARTUP_GRACE_PERIOD_US, std::memory_order_relaxed);
    OS_LOGI(TAG, "Device fully configured, startup grace period of 30s before opening portal");
  }

  if (TaskUtils::TaskCreateExpensive(captiveportal_managertask, "CaptivePortalManager", 6144, nullptr, 1, &s_managerTask) != pdPASS) {
    OS_LOGE(TAG, "Failed to create captive portal manager task");
    return false;
  }

  esp_timer_create_args_t args = {
    .callback              = captiveportal_timernotify,
    .arg                   = nullptr,
    .dispatch_method       = ESP_TIMER_TASK,
    .name                  = "captive_portal_update",
    .skip_unhandled_events = true,
  };

  esp_err_t err;

  err = esp_timer_create(&args, &s_captivePortalUpdateLoopTimer);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to create captive portal update timer");
    return false;
  }

  err = esp_timer_start_periodic(s_captivePortalUpdateLoopTimer, 500'000);  // 500ms
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to start captive portal update timer");
    return false;
  }

  return true;
}

void CaptivePortal::SetUserDone()
{
  s_userDone = true;
}

void CaptivePortal::SetAlwaysEnabled(bool alwaysEnabled)
{
  s_alwaysEnabled  = alwaysEnabled;
  s_closeRequested = !alwaysEnabled;
  Config::SetCaptivePortalConfig(Config::CaptivePortalConfig(alwaysEnabled));
}
bool CaptivePortal::IsAlwaysEnabled()
{
  return s_alwaysEnabled;
}

bool CaptivePortal::ForceClose(uint32_t timeoutMs)
{
  s_forceClosed = true;

  // Waiting for the lifecycle lock means no start is half-way through (which could still mount the static
  // filesystem after we return); once held, stop the portal here and return only when it is fully torn down.
  if (!s_lifecycleMutex.lock(pdMS_TO_TICKS(timeoutMs))) {
    return false;
  }

  captiveportal_stop();

  s_lifecycleMutex.unlock();

  return true;
}

void CaptivePortal::ReleaseForceClose()
{
  s_forceClosed = false;
}

bool CaptivePortal::IsRunning()
{
  return GetInstance() != nullptr;
}

const char* CaptivePortal::ApIPv4String()
{
  return CAPTIVE_PORTAL_AP_IP;
}

bool CaptivePortal::SendMessageTXT(uint8_t socketId, std::string_view data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  instance->sendMessageTXT(socketId, data);

  return true;
}
bool CaptivePortal::SendMessageBIN(uint8_t socketId, std::span<const uint8_t> data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  instance->sendMessageBIN(socketId, data);

  return true;
}

bool CaptivePortal::BroadcastMessageTXT(std::string_view data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  instance->broadcastMessageTXT(data);

  return true;
}
bool CaptivePortal::BroadcastMessageBIN(std::span<const uint8_t> data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  instance->broadcastMessageBIN(data);

  return true;
}
