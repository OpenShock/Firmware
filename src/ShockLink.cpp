#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include "Arduino.h"
#include <vector>
#include <bitset>
#include <map>
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include "RmtControl.h"
#include <algorithm>
#include "Captive.h"
#include "SPIFFS.h"
#include "HTTPClient.h"

const char *ssid = "Luc-H";
const char *password = "LucNetworkPw12";
const String apiUrl = "api.shocklink.net";

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
TaskHandle_t Task1;
bool operational = false;

struct command_t
{
    uint16_t shockerId;
    uint8_t method;
    uint8_t intensity;
    uint duration;
    std::vector<rmt_data_t> sequence;
    ulong until;
};

std::map<uint, command_t> Commands;
rmt_obj_t *rmt_send = NULL;

void IntakeCommand(uint16_t shockerId, uint8_t method, uint8_t intensity, uint duration)
{
    if (Commands.count(shockerId) > 0 && Commands[shockerId].until >= millis())
    {
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

        IntakeCommand(id, type, intensity, duration);
    }
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

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{

    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.printf("[WSc] Disconnected!\n");
        break;
    case WStype_CONNECTED:
    {
        Serial.printf("[WSc] Connected to url: %s\n", payload);
        SendKeepAlive();
    }
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
        if (it.second.until <= mil)
            continue;
        sequence.insert(sequence.end(), it.second.sequence.begin(), it.second.sequence.end());
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

void setup()
{
    Serial.begin(115200);

    Serial.setDebugOutput(true);

    Serial.println();
    Serial.println();
    Serial.println();

    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    WiFi.mode(WIFI_AP_STA);

    WiFiMulti.addAP(ssid, password);

    File file = SPIFFS.open("/networks.txt", FILE_READ);
    while (file.available())
    {
        String ssid = file.readStringUntil(',');
        String pw = file.readStringUntil(';');
        WiFiMulti.addAP(ssid.c_str(), pw.c_str());
    }
    file.close();

    Captive::Setup();
    Captive::StartCaptive();

    Serial.println("Init WiFi...");
    WiFiMulti.run();

    if (!SPIFFS.exists("/authToken.txt"))
        return;

    if ((rmt_send = rmtInit(13, RMT_TX_MODE, RMT_MEM_64)) == NULL)
    // if ((rmt_send = rmtInit(15, RMT_TX_MODE, RMT_MEM_64)) == NULL)
    {
        Serial.println("init sender failed\n");
        return;
    }

    float realTick = rmtSetTick(rmt_send, 1000);
    Serial.printf("real tick set to: %fns\n", realTick);

    xTaskCreate(Task1code, "RmtLoop",
                10000, /* Stack size in words */
                NULL, 0, &Task1);

    File authTokenFile = SPIFFS.open("/authToken.txt", FILE_READ);
    String authToken = authTokenFile.readString();
    authTokenFile.close();

    webSocket.setExtraHeaders(("DeviceToken: " + authToken).c_str());
    webSocket.beginSSL(apiUrl, 443, "/1/ws/device");
    webSocket.onEvent(webSocketEvent);
    runner.addTask(keepalive);
    keepalive.enable();

    operational = true;
}

unsigned long previousMillis = 0;
unsigned long interval = 30000;
bool firstConnect = true;
bool reconnectedLoop = false;

void loop()
{
    // Serial.print(".");
    unsigned long currentMillis = millis();
    // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
    if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >= interval))
    {
        reconnectedLoop = false;

        Serial.print(millis());
        Serial.println("Reconnecting to WiFi...");
        WiFi.reconnect();
        previousMillis = currentMillis;
    }
    else if (!reconnectedLoop && WiFi.status() == WL_CONNECTED)
    {
        reconnectedLoop = true;
        Serial.print("Connected to wifi, ip: ");
        Serial.println(WiFi.localIP());

        if (firstConnect)
        {
            if (SPIFFS.exists("/pairCode.txt"))
            {
                File file = SPIFFS.open("/pairCode.txt", FILE_READ);
                String pairCode = file.readString();
                file.close();
                SPIFFS.remove("/pairCode.txt");

                HTTPClient http;
                String uri = "https://" + apiUrl + "/1/pair/" + pairCode;

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

                File authTokenFile = SPIFFS.open("/authToken.txt", FILE_WRITE);
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

        Captive::StopCaptive();
        firstConnect = false;
    }

    if (Captive::IsActive())
    {
        Captive::Loop();
        return;
    }

    if(!operational) return;

    webSocket.loop();
    runner.execute();
}
