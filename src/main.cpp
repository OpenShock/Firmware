#include "APIConnection.h"
#include "CaptivePortal.h"
#include "Constants.h"
#include "FileUtils.h"
#include "WiFiManager.h"

#include <esp_log.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <String.h>

#include <memory>

const char* const TAG = "ShockLink";

std::unique_ptr<ShockLink::APIConnection> s_apiConnection = nullptr;

bool setupOk = false;

void setup() {
  Serial.begin(115'200);
  Serial.setDebugOutput(true);

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

  setupOk = true;
}

String inputBuffer = "";

bool writeCommands(String& command, String& data) {
  if (command == "authtoken") {
    return ShockLink::FileUtils::TryWriteFile("/authToken", data);
  }

  if (command == "rmtpin") {
    return ShockLink::FileUtils::TryWriteFile("/rmtPin", data);
  }

  if (command == "networks") {
    return ShockLink::FileUtils::TryWriteFile("/networks", data);
  }

  if (command == "debug") {
    int delimiter     = data.indexOf(' ');
    String subCommand = data.substring(0, delimiter);
    subCommand.toLowerCase();
    if (subCommand != "api") {
      return false;
    }

    data = inputBuffer.substring(delimiter + 1);

    File file     = LittleFS.open("/debug/api", FILE_WRITE);
    uint8_t state = data == "1" ? 1 : 0;
    file.write(state);
    file.close();

    Serial.printf("SYS|Success|Dev api state: %d\n", state);

    return true;
  }

  return false;
}

void executeCommand() {
  int delimiter  = inputBuffer.indexOf(' ');
  String command = inputBuffer.substring(0, delimiter);
  command.toLowerCase();
  String data = inputBuffer.substring(delimiter + 1);

  if (data.length() <= 0) {
    if (command == "restart") {
      Serial.println("Restarting ESP...");
      ESP.restart();
    }
  }

  if (data.length() > 0) {
    if (writeCommands(command, data)) {
      Serial.println("SYS|Success|Command executed");
      return;
    }
  }

  Serial.println("SYS|Error|Command not found or encountered an error");
}

void handleSerial() {
  char data = Serial.read();  // Read the incoming data
  if (data == 0xd) return;

  if (data == 0xa) {
    Serial.printf("> %s\n", inputBuffer.c_str());
    executeCommand();
    inputBuffer = "";
    return;
  }

  inputBuffer += data;
}

void loop() {
  if (!setupOk) return;

  if (Serial.available()) handleSerial();

  ShockLink::CaptivePortal::Update();

  if (s_apiConnection != nullptr) {
    s_apiConnection->Update();
  }
}
