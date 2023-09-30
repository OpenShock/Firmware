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
  OpenShock::SerialInputHandler::Init();
  ESP_LOGI(TAG, "==== OpenShock v%s ====", OpenShock::Constants::Version);

  OpenShock::CommandHandler::Init();

  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS");
    return;
  }

  OpenShock::WiFiManager::Init();
}

void loop() {
  OpenShock::SerialInputHandler::Update();
  OpenShock::CaptivePortal::Update();

  if (s_apiConnection != nullptr) {
    s_apiConnection->Update();
  }
}
