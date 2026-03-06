#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "captiveportal/Manager.h"
#include "captiveportal/RFC8908Handler.h"
#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "estop/EStopManager.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "serialization/WSLocal.h"
#include "util/FnProxy.h"
#include "util/HexUtils.h"
#include "util/PartitionUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

#include <WiFi.h>

const uint16_t HTTP_PORT                 = 80;
const uint16_t WEBSOCKET_PORT            = 81;
const uint16_t DNS_PORT                  = 53;
const uint32_t WEBSOCKET_PING_INTERVAL   = 10'000;
const uint32_t WEBSOCKET_PING_TIMEOUT    = 1000;
const uint8_t WEBSOCKET_PING_RETRIES     = 3;
const uint32_t WEBSOCKET_UPDATE_INTERVAL = 10;  // 10ms / 100Hz

using namespace OpenShock;

static const esp_partition_t* getStaticPartition()
{
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "static0");
  if (partition != nullptr) {
    return partition;
  }

  partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "static1");
  if (partition != nullptr) {
    return partition;
  }

  return nullptr;
}

static const char* getPartitionHash()
{
  const esp_partition_t* partition = getStaticPartition();
  if (partition == nullptr) {
    return nullptr;
  }

  static char hash[65];
  if (!OpenShock::TryGetPartitionHash(partition, hash)) {
    return nullptr;
  }

  return hash;
}

CaptivePortal::CaptivePortalInstance::CaptivePortalInstance()
  : m_webServer(HTTP_PORT)
  , m_socketServer(WEBSOCKET_PORT, "/ws", "flatbuffers")  // Sec-WebSocket-Protocol = flatbuffers
  , m_socketDeFragger(std::bind(&CaptivePortalInstance::handleWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
  , m_fileSystem()
  , m_dnsServer()
  , m_taskHandle(nullptr)
{
  m_socketServer.onEvent(std::bind(&WebSocketDeFragger::handler, &m_socketDeFragger, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  m_socketServer.begin();

  m_socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

  OS_LOGI(TAG, "Setting up DNS server");
  bool dnsStarted = m_dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  if (!dnsStarted) {
    OS_LOGE(TAG, "Failed to start DNS server!");
  }

  bool fsOk = true;

  // Get the hash of the filesystem
  const char* fsHash = getPartitionHash();
  if (fsHash == nullptr) {
    OS_LOGE(TAG, "Failed to get filesystem hash");
    fsOk = false;
  }

  if (fsOk) {
    // Mounting LittleFS
    if (!m_fileSystem.begin(false, "/static", 10U, "static0")) {
      OS_LOGE(TAG, "Failed to mount LittleFS");
      fsOk = false;
    } else {
      fsOk = m_fileSystem.exists("/www/index.html.gz");
    }
  }

  if (fsOk) {
    OS_LOGI(TAG, "Serving files from LittleFS");
    OS_LOGI(TAG, "Filesystem hash: %s", fsHash);

    m_webServer.addHandler(new RFC8908Handler());

    // API endpoints — must be registered before serveStatic so they take priority
    m_webServer.on("/api/portal/close", HTTP_POST, [](AsyncWebServerRequest* request) {
      request->send(200, "text/plain", "Closing portal");
      CaptivePortal::ForceClose(0);
    });

    m_webServer.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest* request) {
      bool run = true;
      if (request->hasParam("run")) {
        run = request->getParam("run")->value().toInt() != 0;
      }
      if (run) {
        WiFiScanManager::StartScan();
      } else {
        WiFiScanManager::AbortScan();
      }
      request->send(200, "application/json", "{\"ok\":true}");
    });

    m_webServer.on("/api/wifi/networks", HTTP_DELETE, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("ssid")) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing ssid parameter\"}");
        return;
      }
      String ssid = request->getParam("ssid")->value();
      if (!WiFiManager::Forget(ssid.c_str())) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"InternalError\"}");
        return;
      }
      request->send(200, "application/json", "{\"ok\":true}");
    });

    m_webServer.on("/api/account/link", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("code")) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"CodeRequired\"}");
        return;
      }
      String code     = request->getParam("code")->value();
      auto result     = GatewayConnectionManager::Link(std::string_view(code.c_str(), code.length()));
      using ResultCode = Serialization::Local::AccountLinkResultCode;
      if (result == ResultCode::Success) {
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }
      const char* error;
      switch (result) {
        case ResultCode::CodeRequired:
          error = "CodeRequired";
          break;
        case ResultCode::InvalidCodeLength:
          error = "InvalidCodeLength";
          break;
        case ResultCode::NoInternetConnection:
          error = "NoInternetConnection";
          break;
        case ResultCode::InvalidCode:
          error = "InvalidCode";
          break;
        case ResultCode::RateLimited:
          error = "RateLimited";
          break;
        default:
          error = "InternalError";
          break;
      }
      String resp = String("{\"ok\":false,\"error\":\"") + error + "\"}";
      request->send(400, "application/json", resp);
    });

    m_webServer.on("/api/account", HTTP_DELETE, [](AsyncWebServerRequest* request) {
      GatewayConnectionManager::UnLink();
      request->send(200, "application/json", "{\"ok\":true}");
    });

    m_webServer.on("/api/config/rf/pin", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("pin")) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"InvalidPin\"}");
        return;
      }
      int pin         = request->getParam("pin")->value().toInt();
      auto result     = CommandHandler::SetRfTxPin(static_cast<gpio_num_t>(pin));
      using ResultCode = Serialization::Local::SetGPIOResultCode;
      if (result == ResultCode::Success) {
        String resp = String("{\"ok\":true,\"pin\":") + pin + "}";
        request->send(200, "application/json", resp);
        return;
      }
      const char* error = (result == ResultCode::InvalidPin) ? "InvalidPin" : "InternalError";
      String resp       = String("{\"ok\":false,\"error\":\"") + error + "\"}";
      request->send(400, "application/json", resp);
    });

    m_webServer.on("/api/config/estop/pin", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("pin")) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"InvalidPin\"}");
        return;
      }
      int8_t pin        = static_cast<int8_t>(request->getParam("pin")->value().toInt());
      const char* error = nullptr;
      if (IsValidInputPin(pin)) {
        if (!EStopManager::SetEStopPin(static_cast<gpio_num_t>(pin))) {
          error = "InternalError";
        } else if (!Config::SetEStopGpioPin(static_cast<gpio_num_t>(pin))) {
          error = "InternalError";
        }
      } else {
        error = "InvalidPin";
      }
      if (error == nullptr) {
        String resp = String("{\"ok\":true,\"pin\":") + pin + "}";
        request->send(200, "application/json", resp);
      } else {
        String resp = String("{\"ok\":false,\"error\":\"") + error + "\"}";
        request->send(400, "application/json", resp);
      }
    });

    m_webServer.on("/api/config/estop/enabled", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("enabled")) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"InternalError\"}");
        return;
      }
      bool enabled = request->getParam("enabled")->value().toInt() != 0;
      bool success = EStopManager::SetEStopEnabled(enabled) && Config::SetEStopEnabled(enabled);
      if (success) {
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"InternalError\"}");
      }
    });

    // Serving the captive portal files from LittleFS
    m_webServer.serveStatic("/", m_fileSystem, "/www/", "max-age=3600").setDefaultFile("index.html").setSharedEtag(fsHash);

    m_webServer.onNotFound(&RFC8908Handler::CatchAll);

  } else {
    OS_LOGE(TAG, "/www/index.html or hash files not found, serving error page");

    m_webServer.onNotFound([](AsyncWebServerRequest* request) {
      request->send(
        200,
        "text/plain",
        // Raw string literal (1+ to remove the first newline)
        1 + R"(
You probably forgot to upload the Filesystem with PlatformIO!
Go to PlatformIO -> Platform -> Upload Filesystem Image!
If this happened with a file we provided or you just need help, come to the Discord!

discord.gg/OpenShock
)"
      );
    });
  }

  m_webServer.begin();

  if (fsOk) {
    if (TaskUtils::TaskCreateExpensive(Util::FnProxy<&CaptivePortal::CaptivePortalInstance::task>, TAG, 8192, this, 1, &m_taskHandle) != pdPASS) {  // PROFILED: 4-6KB stack usage
      OS_LOGE(TAG, "Failed to create task");
    }
  }
}

CaptivePortal::CaptivePortalInstance::~CaptivePortalInstance()
{
  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }
  m_webServer.end();
  m_socketServer.close();
  m_fileSystem.end();
  m_dnsServer.stop();
}

void CaptivePortal::CaptivePortalInstance::task()
{
  while (true) {
    m_socketServer.loop();
    m_dnsServer.processNextRequest();
    vTaskDelay(pdMS_TO_TICKS(WEBSOCKET_UPDATE_INTERVAL));
  }
}

void CaptivePortal::CaptivePortalInstance::handleWebSocketClientConnected(uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u connected from %s", socketId, m_socketServer.remoteIP(socketId).toString().c_str());

  WiFiNetwork connectedNetwork;
  WiFiNetwork* connectedNetworkPtr = nullptr;
  if (WiFiManager::GetConnectedNetwork(connectedNetwork)) {
    connectedNetworkPtr = &connectedNetwork;
  }

  Serialization::Local::SerializeReadyMessage(connectedNetworkPtr, GatewayConnectionManager::IsLinked(), std::bind(&CaptivePortal::CaptivePortalInstance::sendMessageBIN, this, socketId, std::placeholders::_1));

  // Send all previously scanned wifi networks
  auto networks = OpenShock::WiFiManager::GetDiscoveredWiFiNetworks();

  Serialization::Local::SerializeWiFiNetworksEvent(Serialization::Types::WifiNetworkEventType::Discovered, networks, std::bind(&CaptivePortal::CaptivePortalInstance::sendMessageBIN, this, socketId, std::placeholders::_1));
}

void CaptivePortal::CaptivePortalInstance::handleWebSocketClientDisconnected(uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

void CaptivePortal::CaptivePortalInstance::handleWebSocketEvent(uint8_t socketId, WebSocketMessageType type, tcb::span<const uint8_t> payload)
{
  switch (type) {
    case WebSocketMessageType::Connected:
      handleWebSocketClientConnected(socketId);
      break;
    case WebSocketMessageType::Disconnected:
      handleWebSocketClientDisconnected(socketId);
      break;
    case WebSocketMessageType::Text:
      OS_LOGE(TAG, "Message type is not supported");
      break;
    case WebSocketMessageType::Binary:
      MessageHandlers::WebSocket::HandleLocalBinary(socketId, payload);
      break;
    case WebSocketMessageType::Ping:
    case WebSocketMessageType::Pong:
      // Do nothing
      break;
    default:
      m_socketDeFragger.clear();
      OS_LOGE(TAG, "Unknown WebSocket event type: %u", type);
      break;
  }
}
