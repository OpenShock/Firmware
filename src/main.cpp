#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Constants.h"
#include "GatewayConnectionManager.h"
#include "SerialInputHandler.h"
#include "WiFiManager.h"
#include "WiFiScanManager.h"

#include <esp_log.h>
#include <HardwareSerial.h>
#include <LittleFS.h>

#include <memory>

const char* const TAG = "OpenShock";

void setup() {
  Serial.begin(115'200);
  Serial.setDebugOutput(true);

  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while mounting LittleFS, restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  OpenShock::SerialInputHandler::PrintWelcomeHeader();
  OpenShock::SerialInputHandler::PrintVersionInfo();

  OpenShock::CommandHandler::Init();

  if (!OpenShock::WiFiManager::Init()) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while initializing WiFiManager, restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  OpenShock::CaptivePortal::Init();
  OpenShock::GatewayConnectionManager::Init();
}

void loop() {
  OpenShock::SerialInputHandler::Update();
  OpenShock::CaptivePortal::Update();
  OpenShock::GatewayConnectionManager::Update();
  OpenShock::WiFiScanManager::Update();
}
