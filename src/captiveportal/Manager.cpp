#include <freertos/FreeRTOS.h>

#include "captiveportal/Manager.h"

const char* const TAG = "CaptivePortal";

#include "captiveportal/CaptivePortalInstance.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "Core.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <esp_timer.h>

#include "SimpleMutex.h"

#include <atomic>
#include <memory>

using namespace OpenShock;

static std::atomic<bool> s_alwaysEnabled                 = false;
static std::atomic<bool> s_forceClosed                   = false;
static std::atomic<bool> s_userDone                      = false;
static esp_timer_handle_t s_captivePortalUpdateLoopTimer = nullptr;
static SimpleMutex s_instanceMutex;
static std::shared_ptr<CaptivePortal::CaptivePortalInstance> s_instance = nullptr;

// Absolute esp_timer timestamps (microseconds). 0 = not armed.
static std::atomic<int64_t> s_startupGraceExpiry = 0;  // Don't open portal until this time passes
static std::atomic<int64_t> s_autoCloseExpiry    = 0;  // Auto-close AP when no clients connected and device is online

static constexpr int64_t STARTUP_GRACE_PERIOD_US = 30LL * 1'000'000;     // 30 seconds
static constexpr int64_t AUTO_CLOSE_DELAY_US     = 5LL * 60 * 1'000'000; // 5 minutes

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
static void CreateInstance()
{
  ScopedLock lock__(&s_instanceMutex);
  s_instance = std::make_shared<CaptivePortal::CaptivePortalInstance>();
}
static void DestroyInstance()
{
  ScopedLock lock__(&s_instanceMutex);
  s_instance = nullptr;
}

static bool captiveportal_start()
{
  if (GetInstance() != nullptr) {
    OS_LOGD(TAG, "Already started");
    return true;
  }

  OS_LOGI(TAG, "Starting captive portal");

  if (!WiFi.enableAP(true)) {
    OS_LOGE(TAG, "Failed to enable AP mode");
    return false;
  }

  if (!WiFi.softAP((OPENSHOCK_FW_AP_PREFIX + WiFi.macAddress()).c_str())) {
    OS_LOGE(TAG, "Failed to start AP");
    WiFi.enableAP(false);
    return false;
  }

  IPAddress apIP(4, 3, 2, 1);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
    OS_LOGE(TAG, "Failed to configure AP");
    WiFi.softAPdisconnect(true);
    return false;
  }

  std::string hostname;
  if (!Config::GetWiFiHostname(hostname)) {
    OS_LOGE(TAG, "Failed to get WiFi hostname, reverting to default");
    hostname = OPENSHOCK_FW_HOSTNAME;
  }

  CreateInstance();

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

  WiFi.softAPdisconnect(true);
}

static void captiveportal_updateloop(void*)
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

  // Force-closed by user (via /api/portal/close)
  if (s_forceClosed) {
    if (GetInstance() != nullptr) {
      OS_LOGD(TAG, "Force-closing captive portal");
      captiveportal_stop();
    }
    return;
  }

  // User completed setup — close portal once device is fully online
  if (s_userDone && GatewayConnectionManager::IsConnected()) {
    if (GetInstance() != nullptr) {
      OS_LOGI(TAG, "User completed setup, closing captive portal");
      captiveportal_stop();
    }
    return;
  }

  // Auto-close: no clients connected, WiFi + gateway are up, 5 minutes elapsed
  auto instance = GetInstance();
  if (instance != nullptr && !s_alwaysEnabled && GatewayConnectionManager::IsConnected()) {
    if (instance->hasClients()) {
      // Clients still connected — reset timer
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
  if (instance == nullptr) {
    bool commandHandlerOk = CommandHandler::Ok();
    bool shouldStart      = s_alwaysEnabled || !commandHandlerOk || !isDeviceFullyConfigured();
    if (shouldStart) {
      OS_LOGD(TAG, "Starting captive portal");
      captiveportal_start();
    }
  }
}

bool CaptivePortal::Init()
{
  // If device is already fully configured, set a startup grace period before opening portal
  if (isDeviceFullyConfigured()) {
    s_startupGraceExpiry.store(esp_timer_get_time() + STARTUP_GRACE_PERIOD_US, std::memory_order_relaxed);
    OS_LOGI(TAG, "Device fully configured, startup grace period of 30s before opening portal");
  }

  esp_timer_create_args_t args = {
    .callback              = captiveportal_updateloop,
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
  s_alwaysEnabled = alwaysEnabled;
  Config::SetCaptivePortalConfig(Config::CaptivePortalConfig(alwaysEnabled));
}
bool CaptivePortal::IsAlwaysEnabled()
{
  return s_alwaysEnabled;
}

bool CaptivePortal::ForceClose(uint32_t timeoutMs)
{
  s_forceClosed = true;

  if (GetInstance() == nullptr) return true;

  while (timeoutMs > 0) {
    uint32_t delay = std::min(timeoutMs, static_cast<uint32_t>(10U));

    vTaskDelay(pdMS_TO_TICKS(delay));

    timeoutMs -= delay;

    if (GetInstance() == nullptr) return true;
  }

  return false;
}

bool CaptivePortal::IsRunning()
{
  return GetInstance() != nullptr;
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
