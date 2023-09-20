#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include "Arduino.h"
#include <vector>
#include <bitset>
#include <map>
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include "LRmtControl.h"
#include "PetTrainerRmtControl.h"
#include <algorithm>
#include "Captive.h"
#include "SPIFFS.h"
#include "HTTPClient.h"
#include <LedManager.h>

const String shocklinkApiUrl = SHOCKLINK_API_URL;
const String shocklinkFwVersion = SHOCKLINK_FW_VERSION;

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
TaskHandle_t Task1;

struct command_t
{
    std::vector<rmt_data_t> sequence;
    std::vector<rmt_data_t>* zeroSequence;
    ulong until;
};

std::map<uint, command_t> Commands;
rmt_obj_t *rmt_send = NULL;

void IntakeCommand(uint16_t shockerId, uint8_t method, uint8_t intensity, uint duration, uint8_t shockerModel)
{
    // Stop logic
    bool isStop = method == 0;
    if(isStop) {
        method = 2; // Vibrate
        intensity = 0;
        duration = 0;
    }

    std::vector<rmt_data_t> rmtData;
    if(shockerModel == 1) {
        Serial.println("Using pet trainer sequence");
        rmtData = PetTrainerRmtControl::GetSequence(shockerId, method, intensity);
    } else {
        Serial.println("Using small sequence");
        rmtData = LRmtControl::GetSequence(shockerId, method, intensity);
    }

    // Zero sequence
    std::vector<rmt_data_t>* zeroSequence;
    if(Commands.find(shockerId) != Commands.end()) {
        Serial.println("Cmd existed");
        zeroSequence = Commands[shockerId].zeroSequence;
    } else {
        Serial.print("Generating new zero sequence for ");
        Serial.println(shockerId);
        if(shockerModel == 1) {
            zeroSequence = new std::vector<rmt_data_t>(PetTrainerRmtControl::GetSequence(shockerId, 2, 0));
        } else {
            zeroSequence = new std::vector<rmt_data_t>(LRmtControl::GetSequence(shockerId, 2, 0));
        }
    }

    command_t cmd = {
        rmtData,
        zeroSequence,
        millis() + duration
    };
        
    Commands[shockerId] = cmd;
}

void ControlCommand(DynamicJsonDocument &doc)
{
    auto data = doc["Data"];
    for (int it = 0; it < data.size(); it++)
    {
        auto cur = data[it];
        uint8_t minval = 99;
        uint16_t id = static_cast<uint16_t>(cur["Id"]);
        uint8_t type = static_cast<uint8_t>(cur["Type"]);
        uint8_t intensity = std::min(static_cast<uint8_t>(cur["Intensity"]), minval);
        int duration = static_cast<unsigned int>(cur["Duration"]);
        uint8_t model = static_cast<uint8_t>(cur["Model"]);

        IntakeCommand(id, type, intensity, duration, model);
    }
}

void CaptiveControl(DynamicJsonDocument &doc)
{
    bool data = (bool)doc["Data"];

    Serial.print("Captive portal debug: ");
    Serial.println(data);
    if(data) Captive::StartCaptive(); else Captive::StopCaptive();
}

void ParseJson(uint8_t *payload)
{
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    int type = doc["ResponseType"];

    switch (type)
    {
        case 0:
            ControlCommand(doc);
            break;
        case 1:
            CaptiveControl(doc);
            break;
    }
}

void SendKeepAlive()
{
    if (!webSocket.isConnected())
    {
        Serial.print("[");
        Serial.print(millis());
        Serial.print("] ");
        Serial.println("WebSocket is not connected, not sending keep alive online state");
        return;
    }
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] ");
    Serial.println("Sending keep alive online state");
    webSocket.sendTXT("{\"requestType\": 0}");
}

Task keepalive(30000, TASK_FOREVER, &SendKeepAlive);
Scheduler runner;

bool firstWebSocketConnect = true;

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.printf("[WSc] Disconnected!\n");
        break;
    case WStype_CONNECTED:
        if (firstWebSocketConnect)
            Captive::StopCaptive();
        Serial.printf("[WSc] Connected to url: %s\n", payload);
        SendKeepAlive();

        firstWebSocketConnect = false;
        break;
    case WStype_TEXT:
        Serial.printf("[WSc] get text: %s\n", payload);
        ParseJson(payload);
        break;
    case WStype_BIN:
        Serial.printf("[WSc] get binary length: %u\n", length);
        break;
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
        Serial.println("[WSc] Error");
        break;
    }
}

void RmtLoop()
{
    if (Commands.size() <= 0)
        return;

    std::vector<rmt_data_t> sequence;
    
    long mil = millis();
    for (std::pair<const uint, command_t> &it : Commands)
    {
        if(it.second.until <= mil) {
            // Send stop for 300ms more to ensure the thing is stopping
            if(it.second.until + 300 >= mil) {
                sequence.insert(sequence.end(), it.second.zeroSequence->begin(), it.second.zeroSequence->end());
            }
        } else {
            // Regular shocking sequence
            sequence.insert(sequence.end(), it.second.sequence.begin(), it.second.sequence.end());
        }
    }

    std::size_t finalSize = sequence.size();
    if (finalSize <= 0)
        return;

    Serial.print("=>");
    rmtWriteBlocking(rmt_send, sequence.data(), finalSize);
}

void Task1code(void *parameter)
{
    Serial.print("RMT loop running on core ");
    Serial.println(xPortGetCoreID());
    while (true)
    {
        RmtLoop();
    }
}

bool useDevApi() {
    if(!SPIFFS.exists("/debug/api")) return false;
    File file = SPIFFS.open("/debug/api");
    auto data = file.read();
    Serial.println(data);
    return data == 1;
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    Serial.println();
    Serial.println();
    Serial.println();
    Serial.print("==== ShockLink v");
    Serial.print(SHOCKLINK_FW_VERSION);
    Serial.println(" ====");

    LedManager::Loop(WL_IDLE_STATUS, false, 0);

    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    WiFi.mode(WIFI_AP_STA);

    File file = SPIFFS.open("/networks", FILE_READ);
    while (file.available())
    {
        String ssid = file.readStringUntil(',');
        String pw = file.readStringUntil(';');
        WiFiMulti.addAP(ssid.c_str(), pw.c_str());
        Serial.print("Adding network: ");
        Serial.print(ssid);
        Serial.print(" - ");
        Serial.println(pw);
    }
    file.close();

    Captive::Setup();
    Captive::StartCaptive();

    Serial.println("Init WiFi...");
    WiFiMulti.run();

    if (!SPIFFS.exists("/authToken"))
        return;

    int rmtPin = 15;

    if (SPIFFS.exists("/rmtPin"))
    {
        File rmtPinFile = SPIFFS.open("/rmtPin", FILE_READ);
        rmtPin = rmtPinFile.readString().toInt();
        rmtPinFile.close();
    }
    Serial.print("Serial pin is: ");
    Serial.println(rmtPin);

    if ((rmt_send = rmtInit(rmtPin, RMT_TX_MODE, RMT_MEM_64)) == NULL)
    {
        Serial.println("init sender failed\n");
        return;
    }

    float realTick = rmtSetTick(rmt_send, 1000);
    Serial.printf("real tick set to: %fns\n", realTick);

    xTaskCreate(Task1code, "RmtLoop",
                10000, /* Stack size in words */
                NULL, 0, &Task1);

    File authTokenFile = SPIFFS.open("/authToken", FILE_READ);
    String authToken = authTokenFile.readString();
    authTokenFile.close();

    webSocket.setExtraHeaders(("FirmwareVersion:" + shocklinkFwVersion + "\r\nDeviceToken: " + authToken).c_str());
    webSocket.beginSSL(shocklinkApiUrl, 443, "/1/ws/device");
    webSocket.onEvent(webSocketEvent);
    runner.addTask(keepalive);
    keepalive.enable();

    useDevApi();
}

unsigned long previousMillis = 0;
unsigned long interval = 30000;
bool firstConnect = true;
bool reconnectedLoop = false;

String inputBuffer = "";

void writeFile(String name, String& data) {
    File file = SPIFFS.open(name, FILE_WRITE);
    file.print(data);
    file.close();

    Serial.println("SYS|Success|Wrote to file");
}

bool writeCommands(String& command, String& data) {
    if(command == "authtoken") {
        writeFile("/authToken", data);
        return true;
    }

    if(command == "rmtpin") {
        writeFile("/rmtPin", data);
        return true;
    }

    if(command == "networks") {
        writeFile("/networks", data);
        return true;
    }

    if(command == "debug") {
        int delimiter = data.indexOf(' ');
        String subCommand = data.substring(0, delimiter);
        subCommand.toLowerCase();
        if(subCommand != "api") {
            return false;
        }

        data = inputBuffer.substring(delimiter + 1);

        File file = SPIFFS.open("/debug/api", FILE_WRITE);
        uint8_t state = data == "1" ? 1 : 0;
        file.write(state);
        file.close();
        Serial.print("SYS|Success|Dev api state: ");
        Serial.println(state);
        return true;
    }

    return false;
}

void executeCommand() {
    int delimiter = inputBuffer.indexOf(' ');
    String command = inputBuffer.substring(0, delimiter);
    command.toLowerCase();
    String data = inputBuffer.substring(delimiter + 1);

    if(data.length() <= 0) {
        if(command == "restart") {
            Serial.println("Restarting ESP...");
            ESP.restart();
        }
    }

    if(data.length() > 0) if(writeCommands(command, data)) return;

    Serial.println("SYS|Error|Command not found");
}

void handleSerial() {
    char data = Serial.read(); // Read the incoming data
    if(data == 0xd) return;

    if(data == 0xa) {
    
        Serial.print("> ");
        Serial.println(inputBuffer);
        executeCommand();
        inputBuffer = "";
        return;
    }

    inputBuffer += data;
}

void loop()
{
    if (Serial.available()) handleSerial();
    
    unsigned long currentMillis = millis();
    wl_status_t wifiStatus = WiFi.status();
    LedManager::Loop(wifiStatus, webSocket.isConnected(), currentMillis);
    // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
    if ((wifiStatus != WL_CONNECTED) && (currentMillis - previousMillis >= interval))
    {
        reconnectedLoop = false;

        Serial.print(millis());
        Serial.println("Reconnecting to WiFi...");
        WiFi.reconnect();
        previousMillis = currentMillis;
    }
    else if (!reconnectedLoop && wifiStatus == WL_CONNECTED)
    {
        reconnectedLoop = true;
        Serial.print("Connected to wifi, ip: ");
        Serial.println(WiFi.localIP());

        if (firstConnect)
        {
            if (SPIFFS.exists("/pairCode"))
            {
                File file = SPIFFS.open("/pairCode", FILE_READ);
                String pairCode = file.readString();
                file.close();
                SPIFFS.remove("/pairCode");

                HTTPClient http;
                String uri = "https://" + shocklinkApiUrl + "/1/pair/" + pairCode;

                Serial.print("Contacting pair code url: ");
                Serial.println(uri);
                http.begin(uri);

                int responseCode = http.GET();

                if (responseCode != 200)
                {
                    Serial.println("Error while getting auth token");
                    Serial.println(responseCode);
                    Serial.println(http.getString());
                    firstConnect = false;
                    return;
                }

                File authTokenFile = SPIFFS.open("/authToken", FILE_WRITE);
                String response = http.getString();
                DynamicJsonDocument doc(512);
                deserializeJson(doc, response);

                authTokenFile.print(doc["data"].as<String>());
                Serial.print("Got auth token: ");
                Serial.println(doc["data"].as<String>());
                authTokenFile.close();

                http.end();

                ESP.restart();
            }
        }

        firstConnect = false;
    }

    if (Captive::IsActive()) Captive::Loop();


    webSocket.loop();
    runner.execute();
}
