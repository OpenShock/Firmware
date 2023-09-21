#include "AuthenticationManager.h"
#include "CaptivePortal.h"
#include "Constants.h"
#include "Rmt/PetTrainerEncoder.h"
#include "Rmt/XlcEncoder.h"
#include "VisualStateManager.h"

#include <algorithm>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <bitset>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <map>
#include <TaskScheduler.h>
#include <vector>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiMulti.h>

const char* const TAG = "ShockLink";

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
TaskHandle_t rmtLoopTask;

int rmtPin = 15;

struct command_t {
  std::vector<rmt_data_t> sequence;
  std::shared_ptr<std::vector<rmt_data_t>> zeroSequence;
  std::uint64_t until;
};

std::unordered_map<std::uint16_t, command_t> Commands;

std::vector<rmt_data_t>
  GetSequence(std::uint16_t shockerId, std::uint8_t method, std::uint8_t intensity, std::uint8_t shockerModel) {
  switch (shockerModel) {
    case 1:
      return ShockLink::Rmt::PetTrainerEncoder::GetSequence(shockerId, method, intensity);
    case 2:
      return ShockLink::Rmt::XlcEncoder::GetSequence(shockerId, 0, method, intensity);
    default:
      ESP_LOGE(TAG, "Unknown shocker model: %d", shockerModel);
      return {};
  }
}
std::shared_ptr<std::vector<rmt_data_t>> GetZeroSequence(std::uint16_t shockerId, std::uint8_t shockerModel) {
  static std::unordered_map<std::uint16_t, std::shared_ptr<std::vector<rmt_data_t>>> _sequences;

  auto it = _sequences.find(shockerId);
  if (it != _sequences.end()) return it->second;

  std::shared_ptr<std::vector<rmt_data_t>> sequence;
  switch (shockerModel) {
    case 1:
      sequence = std::make_shared<std::vector<rmt_data_t>>(ShockLink::Rmt::PetTrainerEncoder::GetSequence(shockerId, 2, 0));
      break;
    case 2:
      sequence = std::make_shared<std::vector<rmt_data_t>>(ShockLink::Rmt::XlcEncoder::GetSequence(shockerId, 0, 2, 0));
      break;
    default:
      ESP_LOGE(TAG, "Unknown shocker model: %d", shockerModel);
      sequence = nullptr;
      break;
  }

  _sequences[shockerId] = sequence;

  return sequence;
}

void IntakeCommand(uint16_t shockerId, uint8_t method, uint8_t intensity, uint duration, uint8_t shockerModel) {
  // Stop logic
  if (method == 0) {
    method    = 2;  // Vibrate
    intensity = 0;
    duration  = 300;
  }

  Commands[shockerId]
    = {GetSequence(shockerId, method, intensity, shockerModel), GetZeroSequence(shockerId, shockerModel), millis() + duration};
}

void ControlCommand(DynamicJsonDocument& doc) {
  auto data = doc["Data"];
  for (int it = 0; it < data.size(); it++) {
    auto cur          = data[it];
    uint8_t minval    = 99;
    uint16_t id       = static_cast<uint16_t>(cur["Id"]);
    uint8_t type      = static_cast<uint8_t>(cur["Type"]);
    uint8_t intensity = std::min(static_cast<uint8_t>(cur["Intensity"]), minval);
    int duration      = static_cast<unsigned int>(cur["Duration"]);
    uint8_t model     = static_cast<uint8_t>(cur["Model"]);

    IntakeCommand(id, type, intensity, duration, model);
  }
}

void CaptiveControl(DynamicJsonDocument& doc) {
  bool data = (bool)doc["Data"];

  ESP_LOGD(TAG, "Captive portal debug: %s", data ? "true" : "false");
  if (data)
    ShockLink::CaptivePortal::Start();
  else
    ShockLink::CaptivePortal::Stop();
}

void ParseJson(uint8_t* payload) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payload);
  int type = doc["ResponseType"];

  switch (type) {
    case 0:
      ControlCommand(doc);
      break;
    case 1:
      CaptiveControl(doc);
      break;
  }
}

void SendKeepAlive() {
  if (!webSocket.isConnected()) {
    ESP_LOGD(TAG, "WebSocket is not connected, not sending keep alive online state");
    return;
  }
  ESP_LOGD(TAG, "Sending keep alive online state");
  webSocket.sendTXT("{\"requestType\": 0}");
}

Task keepalive(30'000, TASK_FOREVER, &SendKeepAlive);
Scheduler runner;

bool firstWebSocketConnect = true;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      ESP_LOGD(TAG, "[WebSocket] Disconnected");
      break;
    case WStype_CONNECTED:
      if (firstWebSocketConnect) ShockLink::CaptivePortal::Start();
      ESP_LOGD(TAG, "[WebSocket] Connected to %s", payload);
      SendKeepAlive();

      firstWebSocketConnect = false;
      break;
    case WStype_TEXT:
      ESP_LOGD(TAG, "[WebSocket] Received text: %s", payload);
      ParseJson(payload);
      break;
    case WStype_BIN:
      ESP_LOGD(TAG, "[WebSocket] Received binary data of length %u", length);
      break;
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      ESP_LOGD(TAG, "[WebSocket] Error");
      break;
  }
}

void RmtLoop(void* parameter) {
  ESP_LOGD(TAG, "RMT loop running on core %d", xPortGetCoreID());

  rmt_obj_t* rmt_send = NULL;
  if ((rmt_send = rmtInit(rmtPin, RMT_TX_MODE, RMT_MEM_64)) == NULL) {
    ESP_LOGE(TAG, "init sender failed");
    return;
  }

  float realTick = rmtSetTick(rmt_send, 1000);
  ESP_LOGD(TAG, "real tick set to: %fns", realTick);

  while (true) {
    if (Commands.size() <= 0) {
      vTaskDelay(0);  // Give the scheduler a chance to run
      continue;
    }

    long mil = millis();

    // Send queued commands
    for (auto it = Commands.begin(); it != Commands.end();) {
      auto& cmd = it->second;

      bool expired = cmd.until < mil;
      bool empty   = cmd.sequence.size() <= 0;

      // Remove expired or empty commands, else send the command.
      // After sending/receiving a command, move to the next one.
      if (expired || empty) {
        // If the command is not empty, send the zero sequence to stop the shocker
        if (!empty) {
          rmtWriteBlocking(rmt_send, cmd.zeroSequence->data(), cmd.zeroSequence->size());
        }

        // Remove the command and move to the next one
        it = Commands.erase(it);
      } else {
        // Send the command
        rmtWriteBlocking(rmt_send, cmd.sequence.data(), cmd.sequence.size());

        // Move to the next command
        ++it;
      }
    }
  }

  // Yeah, this is never reached, but it's good practice
  rmtDeinit(rmt_send);
}

bool useDevApi() {
  if (!LittleFS.exists("/debug/api")) return false;
  File file = LittleFS.open("/debug/api");
  auto data = file.read();
  ESP_LOGD(TAG, "Dev api state: %d", data);
  return data == 1;
}

void setup() {
  Serial.begin(115'200);
  Serial.setDebugOutput(true);

  ESP_LOGD(TAG, "==== ShockLink v%s ====", ShockLink::Constants::Version);

  ShockLink::VisualStateManager::SetConnectionState(ShockLink::ConnectionState::WiFi_Disconnected);

  if (!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS");
    return;
  }

  WiFi.mode(WIFI_AP_STA);

  File file = LittleFS.open("/networks", FILE_READ);
  while (file.available()) {
    String ssid = file.readStringUntil(',');
    String pw   = file.readStringUntil(';');
    WiFiMulti.addAP(ssid.c_str(), pw.c_str());
    ESP_LOGD(TAG, "Adding network: %s - %s", ssid.c_str(), pw.c_str());
  }
  file.close();

  ShockLink::CaptivePortal::Start();

  ESP_LOGD(TAG, "Init WiFi...");
  WiFiMulti.run();

  File authTokenFile = LittleFS.open("/authToken", FILE_READ);
  if (!authTokenFile) return;

  if (LittleFS.exists("/rmtPin")) {
    File rmtPinFile = LittleFS.open("/rmtPin", FILE_READ);
    rmtPin          = rmtPinFile.readString().toInt();
    rmtPinFile.close();
  }
  ESP_LOGD(TAG, "RMT pin is: %d", rmtPin);

  xTaskCreate(RmtLoop, "RmtLoop", 10'000, nullptr, 0, &rmtLoopTask);

  String authToken = authTokenFile.readString();
  authTokenFile.close();

  webSocket.setExtraHeaders(
    ("FirmwareVersion:" + String(ShockLink::Constants::Version) + "\r\nDeviceToken: " + authToken).c_str());
  webSocket.beginSSL(ShockLink::Constants::ApiDomain, 443, "/1/ws/device");
  webSocket.onEvent(webSocketEvent);
  runner.addTask(keepalive);
  keepalive.enable();

  useDevApi();
}

unsigned long previousMillis = 0;
unsigned long interval       = 30'000;
bool firstConnect            = true;
bool reconnectedLoop         = false;

String inputBuffer = "";

void writeFile(String name, String& data) {
  File file = LittleFS.open(name, FILE_WRITE);
  file.print(data);
  file.close();

  ESP_LOGD(TAG, "SYS|Success|Wrote to file");
}

bool writeCommands(String& command, String& data) {
  if (command == "authtoken") {
    writeFile("/authToken", data);
    return true;
  }

  if (command == "rmtpin") {
    writeFile("/rmtPin", data);
    return true;
  }

  if (command == "networks") {
    writeFile("/networks", data);
    return true;
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

  if (data.length() > 0)
    if (writeCommands(command, data)) return;

  Serial.println("SYS|Error|Command not found");
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
  if (Serial.available()) handleSerial();

  unsigned long currentMillis = millis();
  wl_status_t wifiStatus      = WiFi.status();

  // TODO: This is bad logic, fix it
  ShockLink::ConnectionState connectionState;
  switch (wifiStatus) {
    case WL_DISCONNECTED:
      connectionState = ShockLink::ConnectionState::WiFi_Disconnected;
      break;
    case WL_CONNECTED:
      if (webSocket.isConnected())
        connectionState = ShockLink::ConnectionState::WebSocket_Connected;
      else
        connectionState = ShockLink::ConnectionState::WiFi_Connected;
      break;
    default:
      connectionState = ShockLink::ConnectionState::WiFi_Connecting;
      break;
  }
  ShockLink::VisualStateManager::SetConnectionState(connectionState);

  // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
  if ((wifiStatus != WL_CONNECTED) && (currentMillis - previousMillis >= interval)) {
    reconnectedLoop = false;

    ESP_LOGD(TAG, "WiFi lost, reconnecting...");
    WiFi.reconnect();
    previousMillis = currentMillis;
  } else if (!reconnectedLoop && wifiStatus == WL_CONNECTED) {
    reconnectedLoop = true;
    ESP_LOGD(TAG, "Connected to wifi, ip: %s", WiFi.localIP().toString().c_str());

    firstConnect = false;
  }

  ShockLink::CaptivePortal::Update();

  webSocket.loop();
  runner.execute();
}
