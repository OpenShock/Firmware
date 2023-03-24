#include "Captive.h"
#include <WiFi.h>
#include <DNSServer.h>
#include "SPIFFS.h"

#include "cert.h"
#include "private_key.h"

#include <HTTPSServer.hpp>
#include <HTTPServer.hpp>
#include <SSLCert.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>

namespace Captive
{
  using namespace httpsserver;

  const byte DNS_PORT = 53;
  IPAddress apIP(10, 10, 10, 10); // The default android DNS
  DNSServer dnsServer;
  bool _isActive = false;

  SSLCert cert = SSLCert(
      example_crt_DER, example_crt_DER_len,
      example_key_DER, example_key_DER_len);

  HTTPSServer secureServer = HTTPSServer(&cert);
  HTTPServer insecureServer = HTTPServer();


  bool IsActive()
  {
    return _isActive;
  }

  void useHttps(HTTPRequest *req, HTTPResponse *res) {
    auto url = "https://" + req->getHTTPHeaders()->getValue("Host") + req->getRequestString();
    res->setHeader("Location", url);
    res->setStatusCode(301);
  }

  void handleRoot(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "text/html");
    File file = SPIFFS.open("/index.html", FILE_READ);
    res->println(file.readString().c_str());
  }

  void handleCam(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "text/html");
    File file = SPIFFS.open("/cam.html", FILE_READ);
    res->println(file.readString().c_str());
  }

  void handleCamJs(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "application/javascript");
    File file = SPIFFS.open("/js/qr-scanner.min.js", FILE_READ);
    res->println(file.readString().c_str());
  }

    void handleCamJs2(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "application/javascript");
    File file = SPIFFS.open("/js/qr-scanner-worker.min.js", FILE_READ);
    res->println(file.readString().c_str());
  }

  void handleVue(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "application/javascript");
    File file = SPIFFS.open("/js/vue.global.js", FILE_READ);

    char buffer[512];
    while(file.available()) {
      auto lel = file.readBytes(buffer, 512);
      uint8_t* byteBuffer = reinterpret_cast<uint8_t*>(buffer);
      res->write(byteBuffer, lel);
    }
  }

  void handleGetNetworks(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "text/plain");
    File file = SPIFFS.open("/networks.txt", FILE_READ);
    res->println(file.readString().c_str());
  }

  void handlePostNetworks(HTTPRequest *req, HTTPResponse *res)
  {
    res->setHeader("Content-Type", "text/plain");
    File file = SPIFFS.open("/networks.txt", FILE_WRITE);


    byte buffer[256];
    // HTTPReqeust::requestComplete can be used to check whether the
    // body has been parsed completely.
    while(!(req->requestComplete())) {
      // HTTPRequest::readBytes provides access to the request body.
      // It requires a buffer, the max buffer length and it will return
      // the amount of bytes that have been written to the buffer.
      size_t s = req->readBytes(buffer, 256);

      // The response does not only implement the Print interface to
      // write character data to the response but also the write function
      // to write binary data to the response.
      file.write(buffer, s);
    }

    res->setStatusCode(200);

  }

  void handle404(HTTPRequest *req, HTTPResponse *res)
  {
    req->discardRequestBody();
    res->setStatusCode(404);
  }

  void StopCaptive()
  {
    if (!_isActive)
      return;

    Serial.println("Stopping captive portal...");
    secureServer.stop();
    insecureServer.stop();
    dnsServer.stop();
    _isActive = false;
    WiFi.mode(WIFI_STA);
  }

  void StartCaptive()
  {
    if (_isActive)
      return;

    Serial.println("Starting captive portal...");
    _isActive = true;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-DNSServer");
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(DNS_PORT, "*", apIP);

    ResourceNode *nodeRoot = new ResourceNode("/", "GET", &handleRoot);
    ResourceNode *nodeCam = new ResourceNode("/cam", "GET", &handleCam);

    ResourceNode *nodeCamJs = new ResourceNode("/js/qr-scanner.min.js", "GET", &handleCamJs);
    ResourceNode *nodeCamJs2 = new ResourceNode("/js/qr-scanner-worker.min.js", "GET", &handleCamJs2);
    ResourceNode *nodeVue = new ResourceNode("/js/vue.global.js", "GET", &handleVue);
    
    ResourceNode *node404 = new ResourceNode("", "GET", &handle404);
    ResourceNode *getNetworks = new ResourceNode("/networks", "GET", &handleGetNetworks);
    ResourceNode *postNetworks = new ResourceNode("/networks", "POST", &handlePostNetworks);

    ResourceNode *httpsRedir = new ResourceNode("", "GET", &useHttps);


    insecureServer.setDefaultNode(httpsRedir);
    secureServer.registerNode(nodeRoot);
    secureServer.registerNode(getNetworks);
    secureServer.registerNode(postNetworks);
    secureServer.registerNode(nodeCam);
    secureServer.registerNode(nodeCamJs);
    secureServer.registerNode(nodeCamJs2);
    secureServer.registerNode(nodeVue);
    secureServer.setDefaultNode(node404);

    insecureServer.start();
    secureServer.start();

    if (secureServer.isRunning())
    {
      Serial.println("Server ready.");
    }
  }

  void Loop()
  {
    dnsServer.processNextRequest();
    secureServer.loop();
    insecureServer.loop();
  }
}