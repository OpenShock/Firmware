#include <freertos/FreeRTOS.h>

#include "CaptivePortal.h"

const char* const TAG = "CaptivePortal";

#include "CaptivePortalInstance.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "Core.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <esp_timer.h>

#include <memory>

using namespace OpenShock;

static bool s_alwaysEnabled                              = false;
static bool s_forceClosed                                = false;
static esp_timer_handle_t s_captivePortalUpdateLoopTimer = nullptr;
static std::unique_ptr<CaptivePortalInstance> s_instance = nullptr;

static bool captiveportal_start()
{
  if (s_instance != nullptr) {
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

  IPAddress apIP(4,3,2,1);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
    OS_LOGE(TAG, "Failed to configure AP");
    WiFi.softAPdisconnect(true);
    return false;
  }

  s_instance = std::make_unique<CaptivePortalInstance>();

  return true;
}
static void captiveportal_stop()
{
  if (s_instance == nullptr) {
    OS_LOGD(TAG, "Already stopped");
    return;
  }

  OS_LOGI(TAG, "Stopping captive portal");

  s_instance = nullptr;

  mdns_free();

  WiFi.softAPdisconnect(true);
}

static void captiveportal_updateloop(void*)
{
  bool gatewayConnected = GatewayConnectionManager::IsConnected();
  bool commandHandlerOk = CommandHandler::Ok();
  bool shouldBeRunning  = (s_alwaysEnabled || !gatewayConnected || !commandHandlerOk) && !s_forceClosed;

  if (s_instance == nullptr) {
    if (shouldBeRunning) {
      OS_LOGD(TAG, "Starting captive portal");
      OS_LOGD(TAG, "  alwaysEnabled: %s", s_alwaysEnabled ? "true" : "false");
      OS_LOGD(TAG, "  forceClosed: %s", s_forceClosed ? "true" : "false");
      OS_LOGD(TAG, "  isConnected: %s", gatewayConnected ? "true" : "false");
      OS_LOGD(TAG, "  commandHandlerOk: %s", commandHandlerOk ? "true" : "false");
      captiveportal_start();
    }
    return;
  }

  if (!shouldBeRunning) {
    OS_LOGD(TAG, "Stopping captive portal");
    OS_LOGD(TAG, "  alwaysEnabled: %s", s_alwaysEnabled ? "true" : "false");
    OS_LOGD(TAG, "  forceClosed: %s", s_forceClosed ? "true" : "false");
    OS_LOGD(TAG, "  isConnected: %s", gatewayConnected ? "true" : "false");
    OS_LOGD(TAG, "  commandHandlerOk: %s", commandHandlerOk ? "true" : "false");
    captiveportal_stop();
    return;
  }
}

using namespace OpenShock;

bool CaptivePortal::Init()
{
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

void CaptivePortal::SetAlwaysEnabled(bool alwaysEnabled)
{
  s_alwaysEnabled = alwaysEnabled;
  Config::SetCaptivePortalConfig({
    .alwaysEnabled = alwaysEnabled,
  });
}
bool CaptivePortal::IsAlwaysEnabled()
{
  return s_alwaysEnabled;
}

bool CaptivePortal::ForceClose(uint32_t timeoutMs)
{
  s_forceClosed = true;

  if (s_instance == nullptr) return true;

  while (timeoutMs > 0) {
    uint32_t delay = std::min(timeoutMs, static_cast<uint32_t>(10U));

    vTaskDelay(pdMS_TO_TICKS(delay));

    timeoutMs -= delay;

    if (s_instance == nullptr) return true;
  }

  return false;
}

bool CaptivePortal::IsRunning()
{
  return s_instance != nullptr;
}

bool CaptivePortal::SendMessageTXT(uint8_t socketId, std::string_view data)
{
  if (s_instance == nullptr) return false;

  s_instance->sendMessageTXT(socketId, data);

  return true;
}
bool CaptivePortal::SendMessageBIN(uint8_t socketId, tcb::span<const uint8_t> data)
{
  if (s_instance == nullptr) return false;

  s_instance->sendMessageBIN(socketId, data);

  return true;
}

bool CaptivePortal::BroadcastMessageTXT(std::string_view data)
{
  if (s_instance == nullptr) return false;

  s_instance->broadcastMessageTXT(data);

  return true;
}
bool CaptivePortal::BroadcastMessageBIN(tcb::span<const uint8_t> data)
{
  if (s_instance == nullptr) return false;

  s_instance->broadcastMessageBIN(data);

  return true;
}
