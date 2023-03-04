#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include "Arduino.h"
#include <vector>
#include <bitset>
#include <map>
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include "RmtControl.h"
#include <algorithm>

const char* ssid     = "Luc-H";         // The SSID (name) of the Wi-Fi network you want to connect to
const char* password = "LucNetworkPw12";     // The password of the Wi-Fi network

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
TaskHandle_t Task1;

struct command_t {
  uint16_t shockerId;
  uint8_t method;
  uint8_t intensity;
  uint duration;
  std::vector<rmt_data_t> sequence;
  ulong until;
};

std::map<uint, command_t> Commands;
rmt_obj_t* rmt_send = NULL;

void IntakeCommand(uint16_t shockerId, uint8_t method, uint8_t intensity, uint duration) {
    if(Commands.count(shockerId) > 0 && Commands[shockerId].until >= millis()) {

      // FIXME: Callback in websocket to informat about busy shocker.
      return;
    }

    std::vector<rmt_data_t> rmtData = GetSequence(shockerId, method, intensity);

    command_t cmd = {
      shockerId,
      method,
      intensity,
      duration,
      rmtData,
      millis() + duration
    };
    Commands[shockerId] = cmd;
    Serial.print("Commands in loop: ");
    Serial.println(Commands.size());
}

void ControlCommand(DynamicJsonDocument& doc) {
    auto data = doc["Data"];
    for(int it = 0; it < data.size(); it++) {
      auto cur = data[it];
        Serial.print("Index: ");
        Serial.println(it);
        uint8_t minval = 99;
        uint16_t id = static_cast<uint16_t>(cur["Id"]);
        uint8_t type = static_cast<uint8_t>(cur["Type"]);
        uint8_t intensity = std::min(static_cast<uint8_t>(cur["Intensity"]), minval);
        int duration = static_cast<unsigned int>(cur["Duration"]);

        IntakeCommand(id, type, intensity, duration);
    }
  
}

void ParseJson(uint8_t* payload) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payload);
  int type = doc["ResponseType"];

  Serial.print("Got response type: ");
  Serial.println(type);

  switch(type) {
    case 0:
        ControlCommand(doc);
        break;
  }
}

void SendKeepAlive() {
    if(!webSocket.isConnected()) {
        Serial.println("WebSocket is not connected, not sending keep alive onloine state");
        return;
    } 
    Serial.println("Sending keep alive online state");
    webSocket.sendTXT("{\"requestType\": 0}");
}

Task keepalive(30000, TASK_FOREVER, &SendKeepAlive);
Scheduler runner;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {


    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WSc] Disconnected!\n");
            break;
        case WStype_CONNECTED:
            {
                Serial.printf("[WSc] Connected to url: %s\n",  payload);
                SendKeepAlive();
            }
            break;
        case WStype_TEXT:
            Serial.printf("[WSc] get text: %s\n", payload);
            ParseJson(payload);

            SendKeepAlive();
            break;
        case WStype_BIN:
            Serial.printf("[WSc] get binary length: %u\n", length);
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }

}

void RmtLoop() {

    if(Commands.size() <= 0) {
      return;
    }

    std::vector<rmt_data_t> sequence;

    for (auto& it : Commands) {

        if(it.second.until <= millis()) {
          //Commands.erase(it.first);
          continue;
        }
        sequence.insert(sequence.end(), it.second.sequence.begin(), it.second.sequence.end());
    }

    std::size_t finalSize = sequence.size();

    if(finalSize <= 0) {
      return;
    }

    Serial.print("=>");

    rmt_data_t* arr = new rmt_data_t[finalSize];
    std::copy(sequence.begin(), sequence.end(), arr);
    rmtWriteBlocking(rmt_send, arr, finalSize);
    delete[] arr;
}

void Task1code( void * parameter) {
    Serial.print("Task2 running on core ");
    Serial.println(xPortGetCoreID());
    while(true) {
        RmtLoop();
    }
}

void setup() {
    Serial.begin(115200);

    Serial.setDebugOutput(true);

    Serial.println();
    Serial.println();
    Serial.println();

    Serial.println(millis());

    for(uint8_t t = 2; t > 0; t--) {
          Serial.printf("[SETUP] BOOT WAIT %d...\n", t);
          Serial.flush();
         delay(1000);
    }

    xTaskCreate(
          Task1code, /* Function to implement the task */
          "Task1", /* Name of the task */
          10000,  /* Stack size in words */
          NULL,  /* Task input parameter */
          0,  /* Priority of the task */
          &Task1  /* Task handle. */);

    WiFiMulti.addAP(ssid, password);

    //WiFi.disconnect();
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }


    if ((rmt_send = rmtInit(13, RMT_TX_MODE, RMT_MEM_64)) == NULL)
    {
        Serial.println("init sender failed\n");
    }

    float realTick = rmtSetTick(rmt_send, 1000);
    Serial.printf("real tick set to: %fns\n", realTick);

  	

    webSocket.setExtraHeaders("DeviceToken: 3Bi7vPOr5YWIngZqJm81lg8eY2REVEyBTa63J89Xvut0EUKL0p7BtkFIhEMEQJya7xddu2iSyFhIIGy0fiMtgKYpHSvQkUmSTL6AiRArYz0ZJC2ykWhmFHMh28kDKJnBhtaNYaA5xvlRWCfR942ROocjJdaA7StHmrVMCWoO51HDNwGreN48ntnxFbRzrCF5m3Svc6Hr0iGeRcAnWve1ZjgIJMYVqz95qpEG0VXPeS6ppPt0H6fjuW8LuFmzuX10");
    webSocket.beginSSL("10.0.0.4", 443, "/1/ws/device");
    webSocket.onEvent(webSocketEvent);
    runner.addTask(keepalive);
    keepalive.enable();
}

void loop() {
    webSocket.loop();
    runner.execute();
}
