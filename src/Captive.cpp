#include "Captive.h"
#include <WiFi.h>
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"

namespace Captive
{
  const byte DNS_PORT = 53;
  IPAddress apIP(10, 10, 10, 10); // The default android DNS
  bool _isActive = false;

  AsyncWebServer server(80);

  bool IsActive()
  {
    return _isActive;
  }

  void notFound(AsyncWebServerRequest *request) {
      request->send(404, "text/plain", "Not found");
  }

  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){

    if (request->method() == HTTP_POST && request->url() == "/networks") {
      File file = SPIFFS.open("/networks.txt", FILE_WRITE);
      file.write(data, len);
      request->send(200, "text/plain", "Saved");
    }
  }

  void StopCaptive()
  {
    if (!_isActive) return;

    Serial.println("Stopping captive portal...");
    server.end();
    _isActive = false;
  }

  void Setup() {
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    server.onRequestBody(handleBody);

    server.on("/reset", HTTP_POST, [] (AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Resetting...");
        ESP.restart();
    });

    server.on("/networks", HTTP_GET, [] (AsyncWebServerRequest *request) {
        File file = SPIFFS.open("/networks.txt", FILE_READ);
        request->send(200, "text/plain", file.readString().c_str());
    });

    server.onNotFound(notFound);
  }

  void StartCaptive()
  {
    if (_isActive)
      return;

    Serial.println("Starting captive portal...");
    _isActive = true;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP((String("ShockLink-") + WiFi.macAddress()).c_str());
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    server.begin();

    Serial.println("Server ready.");
  
  }

  void Loop()
  {

  }
}