#include "CaptivePortal.h"
#include "AuthenticationManager.h"
#include "Constants.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include "Arduino.h"
#include <vector>
#include <bitset>
#include <map>
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include "XlcRmtControl.h"
#include "PetTrainerRmtControl.h"
#include <algorithm>
#include <LittleFS.h>
#include "HTTPClient.h"
#include <LedManager.h>

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
TaskHandle_t Task1;

struct command_t
{
    std::vector<rmt_data_t> sequence;
    std::vector<rmt_data_t> *zeroSequence;
    ulong until;
};

std::map<uint, command_t> Commands;
rmt_obj_t *rmt_send = NULL;

void IntakeCommand(uint16_t shockerId, uint8_t method, uint8_t intensity, uint duration, uint8_t shockerModel)
{
    // Stop logic
    bool isStop = method == 0;
    if (isStop)
    {
        method = 2; // Vibrate
        intensity = 0;
        duration = 0;
    }

    std::vector<rmt_data_t> rmtData;
    if (shockerModel == 1)
    {
        ESP_LOGD(TAG, "Using pet trainer sequence");
        rmtData = ShockLink::PetTrainerRmtControl::GetSequence(shockerId, method, intensity);
    }
    else
    {
        ESP_LOGD(TAG, "Using XLC sequence");
        rmtData = ShockLink::XlcRmtControl::GetSequence(shockerId, method, intensity);
    }

    // Zero sequence
    std::vector<rmt_data_t> *zeroSequence;
    if (Commands.find(shockerId) != Commands.end())
    {
        ESP_LOGD(TAG, "Command existed");
        zeroSequence = Commands[shockerId].zeroSequence;
    }
    else
    {
        ESP_LOGD(TAG, "Generating new zero sequence for %d", shockerId);
        if (shockerModel == 1)
        {
            zeroSequence = new std::vector<rmt_data_t>(PetTrainerRmtControl::GetSequence(shockerId, 2, 0));
        }
        else
        {
            zeroSequence = new std::vector<rmt_data_t>(XlcRmtControl::GetSequence(shockerId, 2, 0));
        }
    }

    command_t cmd = {
        rmtData,
        zeroSequence,
        millis() + duration};

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

    ESP_LOGD(TAG, "Captive portal debug: %s", data ? "true" : "false");
    if (data)
        ShockLink::CaptivePortal::Start();
    else
        ShockLink::CaptivePortal::Stop();
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
        ESP_LOGD(TAG, "WebSocket is not connected, not sending keep alive online state");
        return;
    }
    ESP_LOGD(TAG, "Sending keep alive online state");
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
        ESP_LOGD(TAG, "[WebSocket] Disconnected");
        break;
    case WStype_CONNECTED:
        if (firstWebSocketConnect)
            ShockLink::CaptivePortal::Start();
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

void RmtLoop()
{
    if (Commands.size() <= 0)
        return;

    std::vector<rmt_data_t> sequence;

    long mil = millis();
    for (std::pair<const uint, command_t> &it : Commands)
    {
        if (it.second.until <= mil)
        {
            // Send stop for 300ms more to ensure the thing is stopping
            if (it.second.until + 300 >= mil)
            {
                sequence.insert(sequence.end(), it.second.zeroSequence->begin(), it.second.zeroSequence->end());
            }
        }
        else
        {
            // Regular shocking sequence
            sequence.insert(sequence.end(), it.second.sequence.begin(), it.second.sequence.end());
        }
    }

    std::size_t finalSize = sequence.size();
    if (finalSize <= 0)
        return;

    ESP_LOGD(TAG, "Sending sequence of size %d", finalSize);
    rmtWriteBlocking(rmt_send, sequence.data(), finalSize);
}

void Task1code(void *parameter)
{
    ESP_LOGD(TAG, "RMT loop running on core %d", xPortGetCoreID());
    while (true)
    {
        RmtLoop();
    }
}

bool useDevApi()
{
    if (!LittleFS.exists("/debug/api"))
        return false;
    File file = LittleFS.open("/debug/api");
    auto data = file.read();
    ESP_LOGD(TAG, "Dev api state: %d", data);
    return data == 1;
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    ESP_LOGD(TAG, "==== ShockLink v%s ====", ShockLink::Constants::Version);

    LedManager::Loop(WL_IDLE_STATUS, false, 0);

    if (!LittleFS.begin(true))
    {
        ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS");
        return;
    }

    WiFi.mode(WIFI_AP_STA);

    File file = LittleFS.open("/networks", FILE_READ);
    while (file.available())
    {
        String ssid = file.readStringUntil(',');
        String pw = file.readStringUntil(';');
        WiFiMulti.addAP(ssid.c_str(), pw.c_str());
        ESP_LOGD(TAG, "Adding network: %s - %s", ssid.c_str(), pw.c_str());
    }
    file.close();

    ShockLink::CaptivePortal::Start();

    ESP_LOGD(TAG, "Init WiFi...");
    WiFiMulti.run();

    File authTokenFile = LittleFS.open("/authToken", FILE_READ);
    if (!authTokenFile)
        return;

    int rmtPin = 15;

    if (LittleFS.exists("/rmtPin"))
    {
        File rmtPinFile = LittleFS.open("/rmtPin", FILE_READ);
        rmtPin = rmtPinFile.readString().toInt();
        rmtPinFile.close();
    }
    ESP_LOGD(TAG, "Serial pin is: %d", rmtPin);

    if ((rmt_send = rmtInit(rmtPin, RMT_TX_MODE, RMT_MEM_64)) == NULL)
    {
        ESP_LOGE(TAG, "init sender failed");
        return;
    }

    float realTick = rmtSetTick(rmt_send, 1000);
    ESP_LOGD(TAG, "real tick set to: %fns", realTick);

    xTaskCreate(Task1code, "RmtLoop",
                10000, /* Stack size in words */
                NULL, 0, &Task1);

    String authToken = authTokenFile.readString();
    authTokenFile.close();

    webSocket.setExtraHeaders(("FirmwareVersion:" + String(ShockLink::Constants::Version) + "\r\nDeviceToken: " + authToken).c_str());
    webSocket.beginSSL(ShockLink::Constants::ApiDomain, 443, "/1/ws/device");
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

void writeFile(String name, String &data)
{
    File file = LittleFS.open(name, FILE_WRITE);
    file.print(data);
    file.close();

    ESP_LOGI(TAG, "SYS|Success|Wrote to file");
}

bool writeCommands(String &command, String &data)
{
    if (command == "authtoken")
    {
        writeFile("/authToken", data);
        return true;
    }

    if (command == "rmtpin")
    {
        writeFile("/rmtPin", data);
        return true;
    }

    if (command == "networks")
    {
        writeFile("/networks", data);
        return true;
    }

    if (command == "debug")
    {
        int delimiter = data.indexOf(' ');
        String subCommand = data.substring(0, delimiter);
        subCommand.toLowerCase();
        if (subCommand != "api")
        {
            return false;
        }

        data = inputBuffer.substring(delimiter + 1);

        File file = LittleFS.open("/debug/api", FILE_WRITE);
        uint8_t state = data == "1" ? 1 : 0;
        file.write(state);
        file.close();

        Serial.printf("SYS|Success|Dev api state: %d\n", state);

        return true;
    }

    return false;
}

void executeCommand()
{
    int delimiter = inputBuffer.indexOf(' ');
    String command = inputBuffer.substring(0, delimiter);
    command.toLowerCase();
    String data = inputBuffer.substring(delimiter + 1);

    if (data.length() <= 0)
    {
        if (command == "restart")
        {
            Serial.println("Restarting ESP...");
            ESP.restart();
        }
    }

    if (data.length() > 0)
        if (writeCommands(command, data))
            return;

    Serial.println("SYS|Error|Command not found");
}

void handleSerial()
{
    char data = Serial.read(); // Read the incoming data
    if (data == 0xd)
        return;

    if (data == 0xa)
    {
        Serial.printf("> %s\n", inputBuffer.c_str());
        executeCommand();
        inputBuffer = "";
        return;
    }

    inputBuffer += data;
}

void loop()
{
    if (Serial.available())
        handleSerial();

    unsigned long currentMillis = millis();
    wl_status_t wifiStatus = WiFi.status();
    LedManager::Loop(wifiStatus, webSocket.isConnected(), currentMillis);
    // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
    if ((wifiStatus != WL_CONNECTED) && (currentMillis - previousMillis >= interval))
    {
        reconnectedLoop = false;

        ESP_LOGI(TAG, "WiFi lost, reconnecting...");
        WiFi.reconnect();
        previousMillis = currentMillis;
    }
    else if (!reconnectedLoop && wifiStatus == WL_CONNECTED)
    {
        reconnectedLoop = true;
        ESP_LOGI(TAG, "Connected to wifi, ip: %s", WiFi.localIP().toString().c_str());

        firstConnect = false;
    }

    ShockLink::CaptivePortal::Update();

    webSocket.loop();
    runner.execute();
}
