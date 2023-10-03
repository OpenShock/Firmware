#include "APIConnection.h"
#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Constants.h"
#include "FileUtils.h"
#include "SerialInputHandler.h"
#include "WiFiManager.h"

#include <esp_log.h>
#include <HardwareSerial.h>
#include <LittleFS.h>

#include <memory>

const char* const TAG = "OpenShock";

std::unique_ptr<OpenShock::APIConnection> s_apiConnection = nullptr;

void setup() {
  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while mounting LittleFS, restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  OpenShock::SerialInputHandler::Init();
  OpenShock::SerialInputHandler::PrintWelcomeHeader();
  OpenShock::SerialInputHandler::PrintVersionInfo();

  OpenShock::CommandHandler::Init();

  if (!OpenShock::WiFiManager::Init()) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while initializing WiFiManager, restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
}

void loop() {
  OpenShock::SerialInputHandler::Update();
  OpenShock::CaptivePortal::Update();

  if (s_apiConnection != nullptr) {
    s_apiConnection->Update();
  }
}
