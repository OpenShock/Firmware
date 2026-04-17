#include "debug/DebugServer.h"

const char* const TAG = "DebugServer";

#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "network/NetworkManager.h"

#include <ESPAsyncWebServer.h>
#include <esp_netif.h>

#include <cstdio>

using namespace OpenShock;

static constexpr uint16_t kDebugServerPort = 8080;

static AsyncWebServer s_server(kDebugServerPort);
static bool s_started = false;

static const char* activeName(NetworkManager::Interface iface)
{
  switch (iface) {
    case NetworkManager::Interface::WiFi:     return "wifi";
    case NetworkManager::Interface::Ethernet: return "ethernet";
    default:                                  return "none";
  }
}

static void writeIpInto(const char* ifKey, char* out, size_t len)
{
  out[0]             = '\0';
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(ifKey);
  if (netif == nullptr) return;

  esp_netif_ip_info_t info;
  if (esp_netif_get_ip_info(netif, &info) != ESP_OK) return;
  if (info.ip.addr == 0) return;

  std::snprintf(out, len, IPSTR, IP2STR(&info.ip));
}

static void handleNetworkDebug(AsyncWebServerRequest* request)
{
  char wifiIp[16];
  char ethIp[16];
  writeIpInto("WIFI_STA_DEF", wifiIp, sizeof(wifiIp));
  writeIpInto("ETH_DEF", ethIp, sizeof(ethIp));

  char body[384];
  int n = std::snprintf(body, sizeof(body),
    "{"
    "\"active\":\"%s\","
    "\"has_ip\":%s,"
    "\"wifi\":{\"connected\":%s,\"ip\":\"%s\"},"
    "\"eth\":{\"connected\":%s,\"ip\":\"%s\"},"
    "\"gateway\":{\"connected\":%s,\"linked\":%s}"
    "}",
    activeName(NetworkManager::GetActive()),
    NetworkManager::HasIP() ? "true" : "false",
    NetworkManager::IsWiFiConnected() ? "true" : "false",
    wifiIp,
    NetworkManager::IsEthernetConnected() ? "true" : "false",
    ethIp,
    GatewayConnectionManager::IsConnected() ? "true" : "false",
    GatewayConnectionManager::IsLinked() ? "true" : "false"
  );

  if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
    request->send(500, "application/json", "{\"error\":\"FormatOverflow\"}");
    return;
  }

  auto* response = request->beginResponse(200, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

bool DebugServer::Init()
{
  if (s_started) return true;

  s_server.on("/network", HTTP_GET, handleNetworkDebug);
  s_server.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "application/json", "{\"error\":\"NotFound\"}"); });
  s_server.begin();

  s_started = true;
  OS_LOGI(TAG, "Debug server listening on port %u (GET /network)", kDebugServerPort);
  return true;
}
