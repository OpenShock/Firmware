#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
#include "util/RetiredList.h"
#include "util/TaskUtils.h"

#include <atomic>
#include <cstring>
#include <memory>

using namespace OpenShock;

using CaptivePortalInstance = CaptivePortal::CaptivePortalInstance;

// The manager task owns the portal's lifecycle and all of its state. Other tasks never touch that state: they send
// commands as task-notification bits, which also wake the task, and the 500 ms timer sends kCmdTick.
enum : uint32_t {
  kCmdTick              = 1 << 0,
  kCmdAlwaysEnabledSet  = 1 << 1,  // s_alwaysEnabled was changed; take over its value
  kCmdCloseWhenOnline   = 1 << 2,
  kCmdForceClose        = 1 << 3,
  kCmdReleaseForceClose = 1 << 4,
};

// Only ever touched by the manager task.
struct ManagerState {
  bool alwaysEnabled      = false;
  bool closeWhenOnline    = false;  // Setup finished or the backend disabled the portal: close it once online
  bool forceClosed        = false;  // OTA is flashing the static filesystem; cleared by ReleaseForceClose()
  bool forceCloseAckOwed  = false;  // ForceClose() is waiting for s_forceCloseDone
  int64_t startupGraceEnd = 0;      // esp_timer time until which the portal must not open; 0 = no grace period
  int64_t autoCloseAt     = 0;      // esp_timer time at which an idle portal closes; 0 = not armed
};

static constexpr int64_t STARTUP_GRACE_PERIOD_US = 30LL * 1'000'000;      // 30 seconds
static constexpr int64_t AUTO_CLOSE_DELAY_US     = 5LL * 60 * 1'000'000;  // 5 minutes

// The requested always-enabled setting, and what IsAlwaysEnabled() reports. Written by SetAlwaysEnabled().
static std::atomic<bool> s_alwaysEnabled = false;

static TaskHandle_t s_managerTask                        = nullptr;
static esp_timer_handle_t s_captivePortalUpdateLoopTimer = nullptr;
static SemaphoreHandle_t s_forceCloseDone                = nullptr;  // Given by the manager task once force-closed

// The running instance, read by any task that sends through it; only the manager task creates or retires it.
static SimpleMutex s_instanceMutex;
static std::shared_ptr<CaptivePortalInstance> s_instance = nullptr;
// Instances taken down whose destruction waits for other tasks to drop their references; destroyed by the manager task.
static RetiredList<CaptivePortalInstance> s_retiredInstances;
static esp_netif_t* s_apNetif = nullptr;

// The captive portal AP always serves from this fixed address; the DNS server and
// RFC8908 handler reference it too (see CaptivePortal::ApIPv4String()).
static const char* const CAPTIVE_PORTAL_AP_IP = "4.3.2.1";

static void sendCommand(uint32_t command)
{
  if (s_managerTask != nullptr) {
    xTaskNotify(s_managerTask, command, eSetBits);
  }
}

static bool isDeviceFullyConfigured()
{
  std::vector<Config::WiFiCredentials> credentialsList;
  if (!Config::GetWiFiCredentials(credentialsList) || credentialsList.empty()) {
    return false;
  }
  return Config::HasBackendAuthToken();
}

static std::shared_ptr<CaptivePortalInstance> GetInstance()
{
  ScopedLock lock__(&s_instanceMutex);
  return s_instance;
}
static bool CreateInstance()
{
  auto instance = std::make_shared<CaptivePortalInstance>();
  if (!instance->ok()) {
    OS_LOGE(TAG, "Captive portal servers failed to start");
    return false;  // `instance` is destroyed here, nothing was published
  }

  ScopedLock lock__(&s_instanceMutex);
  s_instance = std::move(instance);
  return true;
}
// Unpublishes the instance and destroys it on this task. Other tasks may hold a copy for a moment while sending, so
// wait briefly for them to let go. Returns false if one still holds it; it is then destroyed on a later tick, still on
// this task, once released.
static bool DestroyInstance()
{
  {
    ScopedLock lock__(&s_instanceMutex);
    s_retiredInstances.retire(std::move(s_instance));  // Leaves s_instance null
  }

  for (int i = 0; i < 200; ++i) {  // up to ~2 s
    if (s_retiredInstances.reap()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  OS_LOGW(TAG, "Captive portal instance still referenced elsewhere, destroying it once released");
  return false;
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
static void captiveportal_stop(ManagerState& state)
{
  state.closeWhenOnline = false;
  state.autoCloseAt     = 0;

  if (GetInstance() == nullptr) {
    OS_LOGD(TAG, "Already stopped");
    return;
  }

  OS_LOGI(TAG, "Stopping captive portal");

  DestroyInstance();

  // Drop the AP but keep the STA connection alive.
  esp_wifi_set_mode(WIFI_MODE_STA);
}

static void applyCommands(ManagerState& state, uint32_t commands)
{
  if ((commands & kCmdAlwaysEnabledSet) != 0) {
    state.alwaysEnabled = s_alwaysEnabled.load(std::memory_order_relaxed);
    // Disabled from the backend: close once online rather than after the idle timeout
    state.closeWhenOnline = !state.alwaysEnabled;
  }

  if ((commands & kCmdCloseWhenOnline) != 0) {
    state.closeWhenOnline = true;
  }

  if ((commands & kCmdForceClose) != 0) {
    state.forceClosed       = true;
    state.forceCloseAckOwed = true;
  }

  // Handled after kCmdForceClose: a release always comes after the force-close it ends.
  if ((commands & kCmdReleaseForceClose) != 0) {
    state.forceClosed = false;
  }
}

// Runs the captive-portal state machine once. This does heavy work (starting the portal constructs
// CaptivePortalInstance, which mounts LittleFS and spins up the HTTP/WS/DNS servers), so it runs on the dedicated
// manager task, never on the esp_timer task, whose 3.5KB stack would overflow.
static void captiveportal_tick(ManagerState& state)
{
  // Destroy instances an earlier stop had to leave behind once their last other holder lets go.
  bool allDestroyed = s_retiredInstances.reap();

  // ForceClose() returns only once the portal is fully torn down, so answer it before anything else.
  if (state.forceClosed) {
    if (GetInstance() != nullptr) {
      OS_LOGD(TAG, "Force-closing captive portal");
      captiveportal_stop(state);
      allDestroyed = s_retiredInstances.reap();
    }
    if (state.forceCloseAckOwed && allDestroyed) {
      state.forceCloseAckOwed = false;
      xSemaphoreGive(s_forceCloseDone);
    }
    return;
  }

  int64_t now    = esp_timer_get_time();
  bool connected = GatewayConnectionManager::IsConnected();

  // Startup grace period: device is fully configured, wait for the gateway connection before opening the portal.
  if (state.startupGraceEnd != 0) {
    if (!connected && now < state.startupGraceEnd) {
      return;
    }
    state.startupGraceEnd = 0;  // Connected (the portal stays closed) or grace expired (it opens normally)
    if (connected) {
      return;
    }
  }

  bool running    = false;
  bool hasClients = false;
  if (auto instance = GetInstance(); instance != nullptr) {
    // Only sample it: holding the reference across captiveportal_stop() would stall DestroyInstance()
    running    = true;
    hasClients = instance->hasClients();
  }

  if (state.closeWhenOnline && connected) {
    if (running) {
      OS_LOGI(TAG, "Setup completed or portal disabled, closing captive portal");
    }
    captiveportal_stop(state);
    return;
  }

  // Auto-close: no clients connected, WiFi + gateway are up, 5 minutes elapsed
  if (running && !state.alwaysEnabled && connected && !hasClients) {
    if (state.autoCloseAt == 0) {
      state.autoCloseAt = now + AUTO_CLOSE_DELAY_US;
    } else if (now >= state.autoCloseAt) {
      OS_LOGI(TAG, "Auto-closing captive portal AP (no clients for 5 minutes)");
      captiveportal_stop(state);
      return;
    }
  } else {
    state.autoCloseAt = 0;
  }

  // Open portal if not running and device needs setup. Not while a retired instance still holds the static
  // filesystem and ports 80/53: the new one would fail to start and flap the AP on every tick.
  if (!running && allDestroyed && (state.alwaysEnabled || !CommandHandler::Ok() || !isDeviceFullyConfigured())) {
    captiveportal_start();
  }
}

static void captiveportal_managertask(void* arg)
{
  ManagerState state = *static_cast<ManagerState*>(arg);
  delete static_cast<ManagerState*>(arg);

  for (;;) {
    uint32_t commands = 0;
    xTaskNotifyWait(0, UINT32_MAX, &commands, portMAX_DELAY);

    applyCommands(state, commands);
    captiveportal_tick(state);
  }
}

// esp_timer callback (runs on the esp_timer task, 3.5KB stack). Must stay trivial:
// it only kicks the manager task, which does the real work on its own stack.
static void captiveportal_timernotify(void*)
{
  sendCommand(kCmdTick);
}

bool CaptivePortal::Init()
{
  auto* initialState = new ManagerState();

  // Restore the persisted always-enabled setting before the hook can change it, so it survives a reboot.
  Config::CaptivePortalConfig config;
  if (Config::GetCaptivePortalConfig(config)) {
    initialState->alwaysEnabled = config.alwaysEnabled;
    s_alwaysEnabled             = config.alwaysEnabled;
  } else {
    OS_LOGE(TAG, "Failed to get captive portal config");
  }

  // If device is already fully configured, set a startup grace period before opening portal
  if (isDeviceFullyConfigured()) {
    initialState->startupGraceEnd = esp_timer_get_time() + STARTUP_GRACE_PERIOD_US;
    OS_LOGI(TAG, "Device fully configured, startup grace period of 30s before opening portal");
  }

  s_forceCloseDone = xSemaphoreCreateBinary();
  if (s_forceCloseDone == nullptr) {
    OS_LOGE(TAG, "Failed to create captive portal force-close semaphore");
    delete initialState;
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(captiveportal_managertask, "CaptivePortalManager", 6144, initialState, 1, &s_managerTask) != pdPASS) {
    OS_LOGE(TAG, "Failed to create captive portal manager task");
    delete initialState;
    return false;
  }

  AppHooks::RegisterCaptivePortal(CaptivePortal::BroadcastMessageBIN, CaptivePortal::SetAlwaysEnabled);

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
  sendCommand(kCmdCloseWhenOnline);
}

void CaptivePortal::SetAlwaysEnabled(bool alwaysEnabled)
{
  s_alwaysEnabled = alwaysEnabled;
  Config::SetCaptivePortalConfig(Config::CaptivePortalConfig(alwaysEnabled));
  sendCommand(kCmdAlwaysEnabledSet);
}
bool CaptivePortal::IsAlwaysEnabled()
{
  return s_alwaysEnabled;
}

bool CaptivePortal::ForceClose(uint32_t timeoutMs)
{
  if (s_managerTask == nullptr) {
    return GetInstance() == nullptr;
  }

  xSemaphoreTake(s_forceCloseDone, 0);  // Drop an acknowledgement left over from an earlier, timed-out call
  sendCommand(kCmdForceClose);

  // The manager task stops the portal and answers once it is fully torn down (filesystem unmounted). It stays
  // force-closed even if this times out.
  return xSemaphoreTake(s_forceCloseDone, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void CaptivePortal::ReleaseForceClose()
{
  sendCommand(kCmdReleaseForceClose);
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
