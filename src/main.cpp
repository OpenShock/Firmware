#include <freertos/FreeRTOS.h>

const char* const TAG = "main";

#include "captiveportal/Manager.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "estop/EStopManager.h"
#include "events/Events.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "OpenShock.h"
#include "OtaUpdateManager.h"
#include "Panic.h"
#include "serial/SerialInputHandler.h"
#include "util/TaskUtils.h"
#include "visual/VisualStateManager.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include <memory>

// Internal setup function, returns true if setup succeeded, false otherwise.
bool trySetup()
{
  if (!OpenShock::VisualStateManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize VisualStateManager");
    return false;
  }

  if (!OpenShock::EStopManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize EStopManager");
    return false;
  }

  if (!OpenShock::SerialInputHandler::Init()) {
    OS_LOGE(TAG, "Unable to initialize SerialInputHandler");
    return false;
  }

  if (!OpenShock::CommandHandler::Init()) {
    OS_LOGW(TAG, "Unable to initialize CommandHandler");
    return false;
  }

  if (!OpenShock::WiFiManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize WiFiManager");
    return false;
  }

  if (!OpenShock::GatewayConnectionManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize GatewayConnectionManager");
    return false;
  }

  if (!OpenShock::CaptivePortal::Init()) {
    OS_LOGE(TAG, "Unable to initialize CaptivePortal");
    return false;
  }

  return true;
}

// OTA setup is the same as normal setup, but we invalidate the currently running app, and roll back if it fails.
void otaSetup()
{
  OS_LOGI(TAG, "Validating OTA app");

  if (!trySetup()) {
    OS_LOGE(TAG, "Unable to validate OTA app, rolling back");
    OpenShock::OtaUpdateManager::InvalidateAndRollback();
  }

  OS_LOGI(TAG, "Marking OTA app as valid");

  OpenShock::OtaUpdateManager::ValidateApp();

  OS_LOGI(TAG, "Done validating OTA app");
}

// App setup is the same as normal setup, but we restart if it fails.
void appSetup()
{
  if (!trySetup()) {
    OS_LOGI(TAG, "Restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
  }
}

void main_app(void* arg)
{
  while (true) {
    OpenShock::GatewayConnectionManager::Update();

    vTaskDelay(5);  // 5 ticks update interval
  }
}

// ESP-IDF application entry point. Runs the one-time setup, then spawns the
// long-lived main task. When app_main returns, its own task is torn down by IDF
// while the main_app task keeps the firmware running.
extern "C" void app_main()
{
  OpenShock::Config::Init();

  if (!OpenShock::Events::Init()) {
    OS_PANIC(TAG, "Unable to initialize Events");
  }

  if (!OpenShock::OtaUpdateManager::Init()) {
    OS_PANIC(TAG, "Unable to initialize OTA Update Manager");
  }

  if (OpenShock::OtaUpdateManager::IsValidatingApp()) {
    otaSetup();
  } else {
    appSetup();
  }

  // Start the main task
  if (OpenShock::TaskUtils::TaskCreateExpensive(main_app, "main_app", 8192, nullptr, 1, nullptr) != pdPASS) {  // PROFILED: 6KB stack usage
    OS_PANIC(TAG, "Failed to create main_app task");
  }
}
