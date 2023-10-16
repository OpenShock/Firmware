#include "CaptivePortal.h"

#include "CaptivePortalInstance.h"
#include "Config.h"
#include "GatewayConnectionManager.h"

#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <esp_log.h>

#include <memory>

static const char* TAG = "CaptivePortal";

using namespace OpenShock;

static bool s_alwaysEnabled                              = false;
static std::unique_ptr<CaptivePortalInstance> s_instance = nullptr;

bool _startCaptive() {
  if (s_instance != nullptr) {
    ESP_LOGD(TAG, "Already started");
    return true;
  }

  ESP_LOGI(TAG, "Starting captive portal");

  if (!WiFi.enableAP(true)) {
    ESP_LOGE(TAG, "Failed to enable AP mode");
    return false;
  }

  if (!WiFi.softAP(("OpenShock-" + WiFi.macAddress()).c_str())) {
    ESP_LOGE(TAG, "Failed to start AP");
    WiFi.enableAP(false);
    return false;
  }

  IPAddress apIP(10, 10, 10, 10);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
    ESP_LOGE(TAG, "Failed to configure AP");
    WiFi.softAPdisconnect(true);
    return false;
  }

  s_instance = std::make_unique<CaptivePortalInstance>();

  return true;
}
void _stopCaptive() {
  if (s_instance == nullptr) {
    ESP_LOGD(TAG, "Already stopped");
    return;
  }

  ESP_LOGI(TAG, "Stopping captive portal");

  s_instance = nullptr;

  WiFi.softAPdisconnect(true);
}

using namespace OpenShock;

void CaptivePortal::SetAlwaysEnabled(bool alwaysEnabled) {
  s_alwaysEnabled = alwaysEnabled;
  Config::SetCaptivePortalConfig({
    .alwaysEnabled = alwaysEnabled,
  });
}
bool CaptivePortal::IsAlwaysEnabled() {
  return s_alwaysEnabled;
}

bool CaptivePortal::IsRunning() {
  return s_instance != nullptr;
}
void CaptivePortal::Update() {
  bool shouldBeRunning = s_alwaysEnabled || !GatewayConnectionManager::IsConnected();

  if (s_instance == nullptr) {
    if (shouldBeRunning) {
      _startCaptive();
    }
    return;
  }

  if (!shouldBeRunning) {
    _stopCaptive();
    return;
  }

  s_instance->loop();
}

bool CaptivePortal::SendMessageTXT(std::uint8_t socketId, const char* data, std::size_t len) {
  if (s_instance == nullptr) return false;

  s_instance->sendMessageTXT(socketId, data, len);

  return true;
}
bool CaptivePortal::SendMessageBIN(std::uint8_t socketId, const std::uint8_t* data, std::size_t len) {
  if (s_instance == nullptr) return false;

  s_instance->sendMessageBIN(socketId, data, len);

  return true;
}

bool CaptivePortal::BroadcastMessageTXT(const char* data, std::size_t len) {
  if (s_instance == nullptr) return false;

  s_instance->broadcastMessageTXT(data, len);

  return true;
}
bool CaptivePortal::BroadcastMessageBIN(const std::uint8_t* data, std::size_t len) {
  if (s_instance == nullptr) return false;

  s_instance->broadcastMessageBIN(data, len);

  return true;
}
