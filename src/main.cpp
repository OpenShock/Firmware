#include "APIConnection.h"
#include "CaptivePortal.h"
#include "Constants.h"
#include "FileUtils.h"
#include "SerialInputHandler.h"
#include "WiFiManager.h"

#include <esp_log.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <String.h>

#include <memory>

const char* const TAG = "ShockLink";

std::unique_ptr<ShockLink::APIConnection> s_apiConnection = nullptr;

void setup() {
  ShockLink::SerialInputHandler::Init();

  ESP_LOGI(TAG, "==== ShockLink v%s ====", ShockLink::Constants::Version);

  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS");
    return;
  }

  ShockLink::WiFiManager::Init();

  int rmtPin = 15;
  if (LittleFS.exists("/rmtPin")) {
    File rmtPinFile = LittleFS.open("/rmtPin", FILE_READ);
    rmtPin          = rmtPinFile.readString().toInt();
    rmtPinFile.close();
  }
  ESP_LOGD(TAG, "RMT pin is: %d", rmtPin);
}

void loop() {
  ShockLink::SerialInputHandler::Update();
  ShockLink::CaptivePortal::Update();

  if (s_apiConnection != nullptr) {
    s_apiConnection->Update();
  }
}
