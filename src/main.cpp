#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Config.h"
#include "Constants.h"
#include "EStopManager.h"
#include "event_handlers/Init.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "SerialInputHandler.h"
#include "VisualStateManager.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include <LittleFS.h>

#include <memory>

const char* const TAG = "OpenShock";

void setup() {
  Serial.begin(115'200);

  if (!LittleFS.begin(true)) {
    ESP_PANIC(TAG, "Unable to mount LittleFS");
  }

  OpenShock::EventHandlers::Init();

  OpenShock::VisualStateManager::Init();

  OpenShock::SerialInputHandler::PrintWelcomeHeader();
  OpenShock::SerialInputHandler::PrintVersionInfo();

  OpenShock::EStopManager::Init();

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

void loop() {
  OpenShock::SerialInputHandler::Update();
  OpenShock::CaptivePortal::Update();
  OpenShock::GatewayConnectionManager::Update();
  OpenShock::WiFiManager::Update();
}
