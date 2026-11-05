#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "captiveportal/Manager.h"

const char* const TAG = "CaptivePortal";

#include "AppHooks.h"
#include "BackendConfig.h"
#include "captiveportal/CaptivePortalConfig.h"
#include "captiveportal/CaptivePortalInstance.h"
#include "CommandHandler.h"
#include "FormatHelpers.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "Temporal.h"
#include "wifi/WiFiConfig.h"

#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include "SimpleMutex.h"
#include "util/IPv4Utils.h"
#include "util/RetiredList.h"
#include "util/TaskUtils.h"

#include <algorithm>
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
  bool forceClosed        = false;  // OTA is flashing the www image; cleared by ReleaseForceClose()
  bool forceCloseAckOwed  = false;  // ForceClose() is waiting for s_forceCloseDone
  int64_t startupGraceEnd = 0;      // esp_timer time until which the portal must not open; 0 = no grace period
  int64_t autoCloseAt     = 0;      // esp_timer time at which an idle portal closes; 0 = not armed
};

static constexpr int64_t kStartupGracePeriodUs = CONFIG_OPENSHOCK_PORTAL_STARTUP_GRACE_MS * 1000LL;
static constexpr int64_t kAutoCloseDelayUs     = CONFIG_OPENSHOCK_PORTAL_AUTO_CLOSE_MS * 1000LL;

// The requested always-enabled setting, and what IsAlwaysEnabled() reports. Written by SetAlwaysEnabled().
static std::atomic<bool> s_alwaysEnabled = false;

static TaskHandle_t s_managerTask                        = nullptr;
static esp_timer_handle_t s_captivePortalUpdateLoopTimer = nullptr;
static SemaphoreHandle_t s_forceCloseDone                = nullptr;  // Given by the manager task once force-closed

// The running instance, read by any task that sends through it; only the manager task creates or retires it.
static SimpleMutex s_instanceMutex;
static std::shared_ptr<CaptivePortalInstance> s_instance = nullptr;
// Lock-free mirror of `s_instance != nullptr`, written under s_instanceMutex
static std::atomic<bool> s_instanceLive = false;
// Instances taken down whose destruction waits for other tasks to drop their references; destroyed by the manager task.
static RetiredList<CaptivePortalInstance> s_retiredInstances;
static esp_netif_t* s_apNetif = nullptr;
// Given once WIFI_EVENT_AP_START has added s_apNetif to lwIP, so its interface name can be read.
static SemaphoreHandle_t s_apStarted = nullptr;
// lwIP's name for s_apNetif, which the portal's servers bind to. Re-read on every start: lwIP only knows the netif
// while the AP runs, and gives it a new number each time the AP starts again.
static char s_apIfName[8]                   = {};
static constexpr TickType_t kApStartTimeout = pdMS_TO_TICKS(2000);

// A marker posted to the default event loop: once its handler runs, every event posted before it has been handled
ESP_EVENT_DEFINE_BASE(CAPTIVEPORTAL_PRIVATE_EVENTS);
enum {
  CAPTIVEPORTAL_PRIVATE_EVENT_LOOP_FLUSH,
};
static SemaphoreHandle_t s_loopFlushed = nullptr;

static_assert(IPv4Utils::IsSlash24Host(CONFIG_OPENSHOCK_PORTAL_AP_IP), "CONFIG_OPENSHOCK_PORTAL_AP_IP must be a dotted-decimal IPv4 address with a host octet of 1-254");

static void sendCommand(uint32_t command)
{
  if (s_managerTask != nullptr) {
    xTaskNotifyIndexed(s_managerTask, TaskUtils::kNotifyIndex, command, eSetBits);
  }
}

static bool isDeviceFullyConfigured()
{
  return Config::HasWiFiCredentials() && Config::HasBackendAuthToken();
}

static std::shared_ptr<CaptivePortalInstance> GetInstance()
{
  if (!s_instanceLive.load(std::memory_order_relaxed)) {
    return nullptr;
  }

  ScopedLock lock__(&s_instanceMutex);
  return s_instance;
}
std::shared_ptr<CaptivePortalInstance> CaptivePortal::GetLiveInstance(const CaptivePortalInstance* instance)
{
  auto live = GetInstance();
  return live.get() == instance ? live : nullptr;
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
  s_instanceLive.store(true, std::memory_order_relaxed);
  return true;
}
// Unpublishes the instance and destroys it on this task. Other tasks may hold a copy for a moment while sending, so
// wait briefly for them to let go. Returns false if one still holds it; it is then destroyed on a later tick, still on
// this task, once released.
static bool DestroyInstance()
{
  {
    ScopedLock lock__(&s_instanceMutex);
    s_instanceLive.store(false, std::memory_order_relaxed);
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

// Registered after esp_wifi's default WIFI_EVENT_AP_START handler, so it runs once that one has added the AP netif
// to lwIP: only from then on does lwIP hand out the netif's real interface name.
static void captiveportal_onapstart(void*, esp_event_base_t, int32_t, void*)
{
  xSemaphoreGive(s_apStarted);
}

static void captiveportal_onloopflush(void*, esp_event_base_t, int32_t, void*)
{
  xSemaphoreGive(s_loopFlushed);
}

// Waits until the default event loop has handled everything posted to it before this call
static bool waitForEventLoop(TickType_t timeout)
{
  xSemaphoreTake(s_loopFlushed, 0);  // drop a flush left over from an earlier call that timed out
  if (esp_event_post(CAPTIVEPORTAL_PRIVATE_EVENTS, CAPTIVEPORTAL_PRIVATE_EVENT_LOOP_FLUSH, nullptr, 0, timeout) != ESP_OK) {
    return false;
  }
  return xSemaphoreTake(s_loopFlushed, timeout) == pdTRUE;
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
    esp_netif_str_to_ip4(CONFIG_OPENSHOCK_PORTAL_AP_IP, &ipInfo.ip);
    esp_netif_str_to_ip4(CONFIG_OPENSHOCK_PORTAL_AP_IP, &ipInfo.gw);
    esp_netif_str_to_ip4("255.255.255.0", &ipInfo.netmask);

    esp_netif_dhcps_stop(s_apNetif);
    esp_netif_set_ip_info(s_apNetif, &ipInfo);

    // DHCP option 114 (RFC 8910): points clients straight at the RFC 8908 captive-portal API, so modern Android/iOS
    // can open the portal without waiting for their connectivity probes to be redirected. esp_netif keeps the
    // pointer, so the string must outlive the netif.
    static char captivePortalUri[64];
    snprintf(captivePortalUri, sizeof(captivePortalUri), "http://%s/captive-portal/api", CONFIG_OPENSHOCK_PORTAL_AP_IP);  // RFC8908Handler's API path
    esp_err_t uriErr = esp_netif_dhcps_option(s_apNetif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captivePortalUri, strlen(captivePortalUri));
    if (uriErr != ESP_OK) {
      OS_LOGW(TAG, "Failed to set DHCP captive portal URI: %s", esp_err_to_name(uriErr));
    }

    esp_netif_dhcps_start(s_apNetif);
  }

  // esp_netif_create_default_wifi_ap() registered esp_wifi's default handlers, so this one runs after them.
  static bool apStartHandlerRegistered = false;
  if (!apStartHandlerRegistered) {
    esp_err_t evtErr = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, captiveportal_onapstart, nullptr);
    if (evtErr == ESP_OK) {
      evtErr = esp_event_handler_register(CAPTIVEPORTAL_PRIVATE_EVENTS, CAPTIVEPORTAL_PRIVATE_EVENT_LOOP_FLUSH, captiveportal_onloopflush, nullptr);
    }
    if (evtErr != ESP_OK) {
      OS_LOGE(TAG, "Failed to register the AP start handlers: %s", esp_err_to_name(evtErr));
      return false;
    }
    apStartHandlerRegistered = true;
  }

  // The default SSID is the prefix + this device's STA MAC (matches the old Arduino naming); a name the user set is
  // used as it is. Both are validated by the wifi section (Normalize), so they fit the config struct.
  const std::string apSsid     = Config::GetWiFiAccessPointSSID();
  const std::string apPassword = Config::GetWiFiAccessPointPassword();

  wifi_config_t apConfig = {};
  const std::string ssid = apSsid == CONFIG_OPENSHOCK_FW_AP_PREFIX ? Config::GetDefaultAccessPointSSID() : apSsid;
  apConfig.ap.ssid_len   = std::min(ssid.size(), sizeof(apConfig.ap.ssid));
  memcpy(apConfig.ap.ssid, ssid.data(), apConfig.ap.ssid_len);
  apConfig.ap.channel = 1;
  if (apPassword.empty()) {
    apConfig.ap.authmode = WIFI_AUTH_OPEN;
  } else {
    // WPA3 clients use SAE, which a sniffer that knows the password still can't decrypt; WPA2 clients keep working.
    // PMF stays optional because WPA2 clients may lack it, and the zeroed config would otherwise pick hunt-and-peck only.
    apConfig.ap.authmode         = WIFI_AUTH_WPA2_WPA3_PSK;
    apConfig.ap.pmf_cfg.required = false;
    apConfig.ap.sae_pwe_h2e      = WPA3_SAE_PWE_BOTH;
    std::size_t passwordLen      = std::min(apPassword.size(), sizeof(apConfig.ap.password) - 1);
    memcpy(apConfig.ap.password, apPassword.data(), passwordLen);
  }
  apConfig.ap.max_connection  = CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS;
  apConfig.ap.beacon_interval = 100;

  s_apIfName[0] = '\0';

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

  // The servers bind to the AP's lwIP interface, which only exists once the AP has started. Reading its name earlier
  // returns another interface's (the STA's), and would put the portal on the LAN.
  //
  // Enabling the AP starts it with the config it had, and setting a different config (a new name or password, or the
  // first start after boot) restarts it: stopped, its netif removed from lwIP, then started again under a new name.
  // Both starts are queued on the event loop by now. Reading the name between them gets nothing (the netif is gone)
  // or the first start's (which no longer exists), so let the loop handle them first, then take the start that stays.
  if (!waitForEventLoop(kApStartTimeout)) {
    OS_LOGW(TAG, "The event loop did not catch up with the AP start, the interface name may be stale");
  }
  if (!esp_netif_is_netif_up(s_apNetif)) {
    xSemaphoreTake(s_apStarted, 0);  // a start that has since been stopped again
    if (!esp_netif_is_netif_up(s_apNetif) && xSemaphoreTake(s_apStarted, kApStartTimeout) != pdTRUE) {
      OS_LOGE(TAG, "AP did not start");
      esp_wifi_set_mode(WIFI_MODE_STA);
      return false;
    }
  }

  err = esp_netif_get_netif_impl_name(s_apNetif, s_apIfName);
  if (err != ESP_OK) {
    s_apIfName[0] = '\0';
    OS_LOGE(TAG, "Failed to get the AP interface name: %s", esp_err_to_name(err));
    esp_wifi_set_mode(WIFI_MODE_STA);
    return false;  // the servers could not be bound to the AP, and must not listen on the LAN
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
    // Persisted here rather than by the caller, which may be the websocket task; back-to-back changes coalesce into
    // one write of the latest value.
    if (!Config::SetCaptivePortalConfig(Config::CaptivePortalConfig(state.alwaysEnabled))) {
      OS_LOGE(TAG, "Failed to save captive portal config");
    }
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
// CaptivePortalInstance, which maps the www image and spins up the HTTP/WS/DNS servers), so it runs on the dedicated
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
    instance->retryDrain();
  }

  // Always-enabled wins over setup completion: closing here would only drop the clients and reopen the AP next tick
  if (state.closeWhenOnline && connected && !state.alwaysEnabled) {
    if (running) {
      OS_LOGI(TAG, "Setup completed or portal disabled, closing captive portal");
    }
    captiveportal_stop(state);
    return;
  }

  // Auto-close: no clients connected, WiFi + gateway are up, kAutoCloseDelayUs elapsed
  if (running && !state.alwaysEnabled && connected && !hasClients) {
    if (state.autoCloseAt == 0) {
      state.autoCloseAt = now + kAutoCloseDelayUs;
    } else if (now >= state.autoCloseAt) {
      OS_LOGI(TAG, "Auto-closing captive portal AP (no clients for %d ms)", CONFIG_OPENSHOCK_PORTAL_AUTO_CLOSE_MS);
      captiveportal_stop(state);
      return;
    }
  } else {
    state.autoCloseAt = 0;
  }

  // Open portal if not running and device needs setup. Not while a retired instance still holds the www
  // image and ports 80/53: the new one would fail to start and flap the AP on every tick.
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
    xTaskNotifyWaitIndexed(TaskUtils::kNotifyIndex, 0, UINT32_MAX, &commands, portMAX_DELAY);

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
  const bool alwaysEnabled    = Config::GetCaptivePortalConfig().alwaysEnabled;
  initialState->alwaysEnabled = alwaysEnabled;
  s_alwaysEnabled             = alwaysEnabled;

  // If device is already fully configured, set a startup grace period before opening portal
  if (isDeviceFullyConfigured()) {
    initialState->startupGraceEnd = esp_timer_get_time() + kStartupGracePeriodUs;
    OS_LOGI(TAG, "Device fully configured, startup grace period of %d ms before opening portal", CONFIG_OPENSHOCK_PORTAL_STARTUP_GRACE_MS);
  }

  s_forceCloseDone = xSemaphoreCreateBinary();
  s_apStarted      = xSemaphoreCreateBinary();
  s_loopFlushed    = xSemaphoreCreateBinary();
  if (s_forceCloseDone == nullptr || s_apStarted == nullptr || s_loopFlushed == nullptr) {
    OS_LOGE(TAG, "Failed to create captive portal semaphores");
    delete initialState;
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(captiveportal_managertask, "CaptivePortalManager", CONFIG_OPENSHOCK_PORTAL_MANAGER_TASK_STACK_SIZE, initialState, 1, &s_managerTask) != pdPASS) {
    OS_LOGE(TAG, "Failed to create captive portal manager task");
    delete initialState;
    return false;
  }

  AppHooks::RegisterCaptivePortal(CaptivePortal::BroadcastMessageBIN, CaptivePortal::SetAlwaysEnabled, CaptivePortal::IsRunning);

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
  if (s_managerTask == nullptr) {
    // No manager task to hand the write to yet
    Config::SetCaptivePortalConfig(Config::CaptivePortalConfig(alwaysEnabled));
    return;
  }
  sendCommand(kCmdAlwaysEnabledSet);  // The manager task saves it
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

  // The manager task stops the portal and answers once it is fully torn down (www image unmounted). It stays
  // force-closed even if this times out.
  return xSemaphoreTake(s_forceCloseDone, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void CaptivePortal::ReleaseForceClose()
{
  sendCommand(kCmdReleaseForceClose);
}

bool CaptivePortal::IsRunning()
{
  return s_instanceLive.load(std::memory_order_relaxed);
}

const char* CaptivePortal::ApIPv4String()
{
  return CONFIG_OPENSHOCK_PORTAL_AP_IP;
}

const char* CaptivePortal::ApInterfaceName()
{
  return s_apIfName;
}

bool CaptivePortal::SendMessageTXT(uint8_t socketId, std::string_view data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  return instance->sendMessageTXT(socketId, data);
}
bool CaptivePortal::SendMessageBIN(uint8_t socketId, std::span<const uint8_t> data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  return instance->sendMessageBIN(socketId, data);
}

bool CaptivePortal::BroadcastMessageTXT(std::string_view data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  return instance->broadcastMessageTXT(data);
}
bool CaptivePortal::BroadcastMessageBIN(std::span<const uint8_t> data)
{
  auto instance = GetInstance();
  if (instance == nullptr) return false;

  return instance->broadcastMessageBIN(data);
}
