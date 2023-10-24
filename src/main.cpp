#include "main.h"
#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Config.h"
#include "Constants.h"
#include "EventHandlers/EventHandlers.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "OtaUpdateManager.h"
#include "SerialInputHandler.h"
#include "VisualStateManager.h"
#include "WiFiManager.h"
#include "WiFiScanManager.h"

#include <LittleFS.h>

#include <memory>

const char* const TAG = "OpenShock";

static void (*s_setup)() = normalSetup;
static void (*s_loop)()  = normalLoop;

void setup() {
  Serial.begin(115'200);

  OpenShock::OtaUpdateManager::Init();
  if (OpenShock::OtaUpdateManager::IsPerformingUpdate()) {
    s_setup = OpenShock::OtaUpdateManager::Setup;
    s_loop  = OpenShock::OtaUpdateManager::Update;
  }

  s_setup();
}

void loop() {
  s_loop();
}

void normalSetup() {
  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while mounting LittleFS, restarting in 5 seconds...");
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

void normalLoop() {
  OpenShock::SerialInputHandler::Update();
  OpenShock::CaptivePortal::Update();
  OpenShock::GatewayConnectionManager::Update();
  OpenShock::WiFiScanManager::Update();
  OpenShock::WiFiManager::Update();
}
