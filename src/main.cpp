#include <freertos/FreeRTOS.h>

const char* const TAG = "main";

#include "captiveportal/Manager.h"
#include "CommandHandler.h"
#include "Common.h"
#include "config/Config.h"
#include "estop/EStopManager.h"
#include "events/Events.h"
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

// Arduino setup function
void setup()
{
  OS_SERIAL.begin(115'200);

#if ARDUINO_USB_MODE
  OS_SERIAL_USB.begin(115'200);
#endif

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
}

void main_app(void* arg)
{
  while (true) {
    OpenShock::GatewayConnectionManager::Update();

    vTaskDelay(5);  // 5 ticks update interval
  }
}

void loop()
{
  // Start the main task
  OpenShock::TaskUtils::TaskCreateExpensive(main_app, "main_app", 8192, nullptr, 1, nullptr);  // PROFILED: 6KB stack usage

  // Kill the loop task (Arduino is stinky)
  vTaskDelete(nullptr);
}
