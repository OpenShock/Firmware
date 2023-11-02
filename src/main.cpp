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
#include "Utils/FS.h"

#include <memory>

const char* const TAG = "OpenShock";

void setup() {
  Serial.begin(115'200);

  if (OpenShock::FS::registerPartition("data", "/data", false, false, false) != ESP_OK) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while mounting LittleFS (var), restarting in 5 seconds...");
    delay(5000);
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
    delay(5000);
    ESP.restart();
  }

  if (!OpenShock::GatewayConnectionManager::Init()) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while initializing WiFiScanManager, restarting in 5 seconds...");
    delay(5000);
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

extern "C" void app_main() {
  initArduino();
  setup();
  while (true) {
    loop();
    vTaskDelay(1);
  }
}
