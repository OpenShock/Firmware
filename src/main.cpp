#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Common.h"
#include "config/Config.h"
#include "EStopManager.h"
#include "event_handlers/Init.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "OtaUpdateManager.h"
#include "serial/SerialInputHandler.h"
#include "util/TaskUtils.h"
#include "VisualStateManager.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include <Arduino.h>

#include <memory>

const char* const TAG = "OpenShock";

// Internal setup function, returns true if setup succeeded, false otherwise.
bool trySetup() {
  OpenShock::EventHandlers::Init();

  if (!OpenShock::VisualStateManager::Init()) {
    ESP_PANIC(TAG, "Unable to initialize VisualStateManager");
  }

  OpenShock::EStopManager::Init(100);  // 100ms update interval

  if (!OpenShock::SerialInputHandler::Init()) {
    ESP_LOGE(TAG, "Unable to initialize SerialInputHandler");
    return false;
  }
  ESP_LOGI(TAG, "POGGIES");

  if (!OpenShock::CommandHandler::Init()) {
    ESP_LOGW(TAG, "Unable to initialize CommandHandler");
    return false;
  }
  ESP_LOGI(TAG, "POGGIES");

  if (!OpenShock::WiFiManager::Init()) {
    ESP_LOGE(TAG, "Unable to initialize WiFiManager");
    return false;
  }
  ESP_LOGI(TAG, "POGGIES");

  if (!OpenShock::GatewayConnectionManager::Init()) {
    ESP_LOGE(TAG, "Unable to initialize GatewayConnectionManager");
    return false;
  }
  ESP_LOGI(TAG, "POGGIES");

  return true;
}

// OTA setup is the same as normal setup, but we invalidate the currently running app, and roll back if it fails.
void otaSetup() {
  ESP_LOGI(TAG, "Validating OTA app");

  if (!trySetup()) {
    ESP_LOGE(TAG, "Unable to validate OTA app, rolling back");
    OpenShock::OtaUpdateManager::InvalidateAndRollback();
  }

  ESP_LOGI(TAG, "Marking OTA app as valid");

  OpenShock::OtaUpdateManager::ValidateApp();

  ESP_LOGI(TAG, "Done validating OTA app");
}

// App setup is the same as normal setup, but we restart if it fails.
void appSetup() {
  if (!trySetup()) {
    ESP_LOGI(TAG, "Restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
  }
  ESP_LOGI(TAG, "POGGIES");
}

// Arduino setup function
void setup() {
  Serial.begin(115'200);

  OpenShock::Config::Init();
  ESP_LOGI(TAG, "POGGIES");
  OpenShock::OtaUpdateManager::Init();
  ESP_LOGI(TAG, "POGGIES");
  if (OpenShock::OtaUpdateManager::IsValidatingApp()) {
    otaSetup();
    ESP_LOGI(TAG, "POGGIES");
  } else {
    appSetup();
    ESP_LOGI(TAG, "POGGIES");
  }
}

void main_app(void* arg) {
  ESP_LOGI(TAG, "POGGIES");
  while (true) {
    OpenShock::SerialInputHandler::Update();
    OpenShock::CaptivePortal::Update();
    OpenShock::GatewayConnectionManager::Update();
    OpenShock::WiFiManager::Update();

    vTaskDelay(5);  // 5 ticks update interval
  }
}

void loop() {
  // Start the main task
  OpenShock::TaskUtils::TaskCreateExpensive(main_app, "main_app", 8192, nullptr, 1, nullptr);

  // Kill the loop task (Arduino is stinky)
  vTaskDelete(nullptr);
}
