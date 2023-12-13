#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "config/Config.h"
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

const UBaseType_t MAIN_PRIORITY       = 1;
const std::uint32_t MAIN_STACK_SIZE   = 8192;
const TickType_t MAIN_UPDATE_INTERVAL = 5;

void setup_ota() {
  OpenShock::OtaUpdateManager::LoadConfig();

  if (!OpenShock::WiFiManager::Init()) {
    ESP_PANIC_OTA(TAG, "An Error has occurred while initializing WiFiManager");
  }

  if (!OpenShock::GatewayConnectionManager::Init()) {
    ESP_PANIC_OTA(TAG, "An Error has occurred while initializing GatewayConnectionManager");
  }
}
void main_ota(void* arg) {
  while (true) {
    OpenShock::WiFiManager::Update();

    vTaskDelay(MAIN_UPDATE_INTERVAL);
  }
}

void setup_app() {
  if (!LittleFS.begin(true, "/static", 10, "static0")) {
    ESP_PANIC(TAG, "Unable to mount LittleFS");
  }

  OpenShock::EventHandlers::Init();
  OpenShock::VisualStateManager::Init();

  OpenShock::EStopManager::Init(100);  // 100ms update interval

  OpenShock::Config::Init();

  if (!OpenShock::SerialInputHandler::Init()) {
    ESP_PANIC(TAG, "Unable to initialize SerialInputHandler");
  }

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

    vTaskDelay(MAIN_UPDATE_INTERVAL);
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
    OpenShock::TaskUtils::TaskCreateExpensive(main_ota, "main_ota", MAIN_STACK_SIZE, nullptr, MAIN_PRIORITY, nullptr);
  } else {
    OpenShock::TaskUtils::TaskCreateExpensive(main_app, "main_app", MAIN_STACK_SIZE, nullptr, MAIN_PRIORITY, nullptr);
  }

  // Kill the loop task
  vTaskDelete(nullptr);
}
