#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Config.h"
#include "Constants.h"
#include "EventHandlers/EventHandlers.h"
#include "GatewayConnectionManager.h"
#include "SerialInputHandler.h"
#include "VisualStateManager.h"
#include "WiFiManager.h"
#include "WiFiScanManager.h"
#include "Logging.h"

#include <LittleFS.h>

#include <memory>

const char* const TAG = "OpenShock";

void setup() {
  Serial.begin(115'200);

  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while mounting LittleFS, restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP.restart();
  }

  OpenShock::EventHandlers::Init();

  OpenShock::VisualStateManager::Init();

  OpenShock::SerialInputHandler::PrintWelcomeHeader();
  OpenShock::SerialInputHandler::PrintVersionInfo();

  OpenShock::Config::Init();

  if (!OpenShock::CommandHandler::Init()) {
    ESP_LOGW(TAG, "An Error has occurred while initializing CommandHandler");
  }

  if (!OpenShock::WiFiManager::Init()) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while initializing WiFiManager, restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP.restart();
  }

  if (!OpenShock::GatewayConnectionManager::Init()) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while initializing WiFiScanManager, restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP.restart();
  }
}

void loop() {
  OpenShock::SerialInputHandler::Update();
  OpenShock::CaptivePortal::Update();
  OpenShock::GatewayConnectionManager::Update();
  OpenShock::WiFiScanManager::Update();
  OpenShock::WiFiManager::Update();
}
