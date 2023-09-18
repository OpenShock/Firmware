#include "CaptivePortal.h"

#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>

#include <memory>

static const char *TAG = "CaptivePortal";

constexpr std::uint16_t HTTP_PORT = 80;
constexpr std::uint16_t WEBSOCKET_PORT = 81;

struct CaptivePortalInstance
{
    CaptivePortalInstance() : webServer(HTTP_PORT), socketServer(WEBSOCKET_PORT) {}

    AsyncWebServer webServer;
    WebSocketsServer socketServer;
};
std::unique_ptr<CaptivePortalInstance> s_webServices = nullptr;

void handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t *data, std::size_t len);
void handleHttpRequestBody(AsyncWebServerRequest *request, std::uint8_t *data, std::size_t len, size_t index, size_t total);
void handleHttpNotFound(AsyncWebServerRequest *request);

bool ShockLink::CaptivePortal::Start()
{
    if (s_webServices != nullptr)
    {
        ESP_LOGI(TAG, "Already started");
        return true;
    }

    ESP_LOGI(TAG, "Starting");

    if (!WiFi.enableAP(true))
    {
        ESP_LOGE(TAG, "Failed to enable AP mode");
        return false;
    }

    if (!WiFi.softAP(("ShockLink-" + WiFi.macAddress()).c_str()))
    {
        ESP_LOGE(TAG, "Failed to start AP");
        WiFi.enableAP(false);
        return false;
    }

    IPAddress apIP(10, 10, 10, 10);
    if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)))
    {
        ESP_LOGE(TAG, "Failed to configure AP");
        WiFi.softAPdisconnect(true);
        return false;
    }

    s_webServices = std::make_unique<CaptivePortalInstance>();

    s_webServices->socketServer.onEvent(handleWebSocketEvent);
    s_webServices->socketServer.begin();
    // s_webServices->socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

    s_webServices->webServer.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");
    s_webServices->webServer.on("/networks", HTTP_GET, [](AsyncWebServerRequest *request)
                                {
        File file = LittleFS.open("/networks", FILE_READ);
        request->send(file, "text/plain");
        file.close(); });
    s_webServices->webServer.on("/pairCode", HTTP_GET, [](AsyncWebServerRequest *request)
                                {
        File file = LittleFS.open("/pairCode", FILE_READ);
        request->send(file, "text/plain");
        file.close(); });
    s_webServices->webServer.on("/rmtPin", HTTP_GET, [](AsyncWebServerRequest *request)
                                {
        File file = LittleFS.open("/rmtPin", FILE_READ);
        request->send(file, "text/plain");
        file.close(); });
    s_webServices->webServer.onRequestBody(handleHttpRequestBody);
    s_webServices->webServer.onNotFound(handleHttpNotFound);
    s_webServices->webServer.begin();

    ESP_LOGI(TAG, "Started");

    return true;
}
void ShockLink::CaptivePortal::Stop()
{
    if (s_webServices == nullptr)
    {
        ESP_LOGI(TAG, "Already stopped");
        return;
    }

    ESP_LOGI(TAG, "Stopping");

    s_webServices->webServer.end();
    s_webServices->socketServer.close();

    s_webServices = nullptr;

    WiFi.softAPdisconnect(true);
}
bool ShockLink::CaptivePortal::IsRunning()
{
    return s_webServices != nullptr;
}
void ShockLink::CaptivePortal::Update()
{
    if (s_webServices == nullptr)
    {
        return;
    }

    s_webServices->socketServer.loop();
}
bool ShockLink::CaptivePortal::BroadcastMessageTXT(const char *data, std::size_t len)
{
    if (s_webServices == nullptr)
    {
        return false;
    }

    s_webServices->socketServer.broadcastTXT(data, len);

    return true;
}
bool ShockLink::CaptivePortal::BroadcastMessageBIN(const std::uint8_t *data, std::size_t len)
{
    if (s_webServices == nullptr)
    {
        return false;
    }

    s_webServices->socketServer.broadcastBIN(data, len);

    return true;
}

void handleWebSocketClientConnected(std::uint8_t socketId)
{
    ESP_LOGI(TAG, "WebSocket client #%u connected from %s", socketId, s_webServices->socketServer.remoteIP(socketId).toString().c_str());
}
void handleWebSocketClientDisconnected(std::uint8_t socketId)
{
    ESP_LOGI(TAG, "WebSocket client #%u disconnected", socketId);
}
void handleWebSocketClientMessage(std::uint8_t socketId, WStype_t type, std::uint8_t *data, std::size_t len)
{
    (void)socketId;

    if (type == WStype_t::WStype_TEXT)
    {
        ESP_LOGD(TAG, "WebSocket client #%u sent text message", socketId);
    }
    else if (type == WStype_t::WStype_BIN)
    {
        ESP_LOGD(TAG, "WebSocket client #%u sent binary message", socketId);
    }
    else
    {
        ESP_LOGE(TAG, "WebSocket client #%u sent unknown message type %u", socketId, type);
    }

    if (type != WStype_t::WStype_TEXT)
    {
        return;
    }

    StaticJsonDocument<256> doc;
    auto err = deserializeJson(doc, data, len);
    if (err)
    {
        ESP_LOGE(TAG, "Failed to deserialize message: %s", err.c_str());
        return;
    }

    String str;
    serializeJsonPretty(doc, str);
    ESP_LOGD(TAG, "Message: %s", str.c_str());
}
void handleWebSocketClientPing(std::uint8_t socketId)
{
    ESP_LOGD(TAG, "WebSocket client #%u ping received", socketId);
}
void handleWebSocketClientPong(std::uint8_t socketId)
{
    ESP_LOGD(TAG, "WebSocket client #%u pong received", socketId);
}
void handleWebSocketClientError(std::uint8_t socketId, uint16_t code, const char *message)
{
    ESP_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, code, message);
}
void handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t *data, std::size_t len)
{
    ESP_LOGD(TAG, "WebSocket event: %u", type);
    switch (type)
    {
    case WStype_CONNECTED:
        handleWebSocketClientConnected(socketId);
        break;
    case WStype_DISCONNECTED:
        handleWebSocketClientDisconnected(socketId);
        break;
    case WStype_BIN:
    case WStype_TEXT:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
        handleWebSocketClientMessage(socketId, type, data, len);
        break;
    case WStype_PING:
        handleWebSocketClientPing(socketId);
        break;
    case WStype_PONG:
        handleWebSocketClientPong(socketId);
        break;
    case WStype_ERROR:
        handleWebSocketClientError(socketId, len, reinterpret_cast<char *>(data));
        break;
    default:
        ESP_LOGE(TAG, "Unknown WebSocket event type: %d", type);
        break;
    }
}

void handleHttpPostRequest(AsyncWebServerRequest *request, std::uint8_t *data, std::size_t len, size_t index, size_t total)
{
    if (request->url() == "/networks")
    {
        File file = LittleFS.open("/networks", FILE_WRITE);
        file.write(data, len);
        file.close();
        request->send(200, "text/plain", "Saved");
        return;
    }

    if (request->url() == "/pairCode")
    {
        File file = LittleFS.open("/pairCode", FILE_WRITE);
        file.write(data, len);
        file.close();
        request->send(200, "text/plain", "Saved");
        return;
    }

    if (request->url() == "/rmtPin")
    {
        File file = LittleFS.open("/rmtPin", FILE_WRITE);
        file.write(data, len);
        file.close();
        request->send(200, "text/plain", "Saved");
        return;
    }

    // Default
    request->send(404, "text/plain", "Not found");
}
void handleHttpRequestBody(AsyncWebServerRequest *request, std::uint8_t *data, std::size_t len, size_t index, size_t total)
{
    ESP_LOGD(TAG, "HTTP request body: %s", request->url().c_str());
    switch (request->method())
    {
    case HTTP_POST:
        ESP_LOGD(TAG, "HTTP POST request: %s", request->url().c_str());
        handleHttpPostRequest(request, data, len, index, total);
        break;
    default:
        ESP_LOGE(TAG, "Unsupported HTTP method: %d", request->method());
        request->send(405, "text/plain", "Method not allowed");
        break;
    }
}
void handleHttpNotFound(AsyncWebServerRequest *request)
{
    ESP_LOGD(TAG, "HTTP not found: %s", request->url().c_str());
    request->send(404, "text/plain", "Not found");
}