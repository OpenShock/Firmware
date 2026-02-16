#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "captiveportal/RFC8908Handler.h"
#include "CommandHandler.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "serialization/WSLocal.h"
#include "util/FnProxy.h"
#include "util/HexUtils.h"
#include "util/PartitionUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"

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
    if (TaskUtils::TaskCreateExpensive(&Util::FnProxy<&CaptivePortal::CaptivePortalInstance::task>, TAG, 8192, this, 1, &m_taskHandle) != pdPASS) {  // PROFILED: 4-6KB stack usage
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
