#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Config.h"
#include "Constants.h"
#include "EStopManager.h"
#include "event_handlers/Init.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "OtaUpdateManager.h"
#include "SerialInputHandler.h"
#include "util/TaskUtils.h"
#include "VisualStateManager.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include <LittleFS.h>

#include <memory>

const char* const TAG = "OpenShock";

void setup_ota() {
  OpenShock::OtaUpdateManager::Setup();
}
void main_ota(void* arg) {
  while (true) {
    OpenShock::OtaUpdateManager::Loop();

    vTaskDelay(5);
  }
}

void setup_app() {
  if (!LittleFS.begin(true)) {
    ESP_PANIC(TAG, "Unable to mount LittleFS");
  }

  OpenShock::EventHandlers::Init();
  OpenShock::VisualStateManager::Init();
  OpenShock::SerialInputHandler::PrintWelcomeHeader();
  OpenShock::SerialInputHandler::PrintVersionInfo();

  OpenShock::EStopManager::Init(100);  // 100ms update interval

  OpenShock::Config::Init();

  if (!OpenShock::CommandHandler::Init()) {
    ESP_LOGW(TAG, "Unable to initialize CommandHandler");
  }

  if (!OpenShock::WiFiManager::Init()) {
    ESP_PANIC(TAG, "Unable to initialize WiFiManager");
  }

  if (!OpenShock::GatewayConnectionManager::Init()) {
    ESP_PANIC(TAG, "Unable to initialize GatewayConnectionManager");
  }
}
void main_app(void* arg) {
  while (true) {
    OpenShock::SerialInputHandler::Update();
    OpenShock::CaptivePortal::Update();
    OpenShock::GatewayConnectionManager::Update();
    OpenShock::WiFiManager::Update();

    vTaskDelay(5);
  }
}

void setup() {
  Serial.begin(115'200);

  OpenShock::OtaUpdateManager::Init();
  if (OpenShock::OtaUpdateManager::IsPerformingUpdate()) {
    setup_ota();
  } else {
    setup_app();
  }
}

void loop() {
  // Start the main task
  if (OpenShock::OtaUpdateManager::IsPerformingUpdate()) {
    OpenShock::TaskUtils::TaskCreateExpensive(main_ota, "main_ota", 8192, nullptr, 1, nullptr);
  } else {
    OpenShock::TaskUtils::TaskCreateExpensive(main_app, "main_app", 8192, nullptr, 1, nullptr);
  }

  // Kill the loop task
  vTaskDelete(nullptr);
}
