#include <freertos/FreeRTOS.h>

#include "CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "CommandHandler.h"
#include "event_handlers/WebSocket.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "util/FnProxy.h"
#include "util/HexUtils.h"
#include "util/PartitionUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

#include <esp_http_server.h>

const uint16_t HTTP_PORT                 = 80;
const uint16_t WEBSOCKET_PORT            = 81;
const uint16_t DNS_PORT                  = 53;
const uint32_t WEBSOCKET_PING_INTERVAL   = 10'000;
const uint32_t WEBSOCKET_PING_TIMEOUT    = 1000;
const uint8_t WEBSOCKET_PING_RETRIES     = 3;
const uint32_t WEBSOCKET_UPDATE_INTERVAL = 10;  // 10ms / 100Hz

using namespace OpenShock;

/* clang-format off */
static httpd_uri_t s_errorHandler {
  .uri     = "/*",
  .method  = HTTP_GET,
  .handler = [](httpd_req_t* req) -> esp_err_t {
  httpd_resp_set_status(req, "500 Internal Server Error");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, R"(You probably forgot to upload the Filesystem with PlatformIO!
Go to PlatformIO -> Platform -> Upload Filesystem Image!
If this happened with a file we provided or you just need help, come to the Discord!

discord.gg/OpenShock)", HTTPD_RESP_USE_STRLEN);
  }
};
/* clang-format on */

const esp_partition_t* _getStaticPartition() {
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

const char* _getPartitionHash() {
  const esp_partition_t* partition = _getStaticPartition();
  if (partition == nullptr) {
    return nullptr;
  }

  static char hash[65];
  if (!OpenShock::TryGetPartitionHash(partition, hash)) {
    return nullptr;
  }

  return hash;
}

CaptivePortalInstance::CaptivePortalInstance()
  : m_httpServer(nullptr), m_socketDeFragger(std::bind(&CaptivePortalInstance::handleWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)), m_fileSystem(), m_dnsServer(), m_taskHandle(nullptr) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  ESP_LOGI(TAG, "Starting HTTP server");
  if (httpd_start(&m_httpServer, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    m_httpServer = nullptr;
    return;
  }

  OS_LOGI(TAG, "Setting up DNS server");
  m_dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  bool fsOk = true;

  // Get the hash of the filesystem
  const char* fsHash = _getPartitionHash();
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

    char softAPURL[64];
    snprintf(softAPURL, sizeof(softAPURL), "http://%s", WiFi.softAPIP().toString().c_str());

    // Serving the captive portal files from LittleFS
    m_webServer.serveStatic("/", m_fileSystem, "/www/", "max-age=3600").setDefaultFile("index.html").setSharedEtag(fsHash);
    httpd_uri_t webSockerHandler {
      .uri                      = "/ws",
      .method                   = HTTP_GET,
      .handler                  =,
      .user_ctx                 = this,
      .is_websocket             = true,
      .handle_ws_control_frames = true,
      .supported_subprotocol    = nullptr,
    };
    httpd_register_uri_handler(m_httpServer, &webSockerHandler);

    httpd_uri_t staticFileHandler {
      .uri                      = "/*",
      .method                   = HTTP_GET,
      .handler                  =,
      .user_ctx                 = this,
      .is_websocket             = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol    = nullptr,
    };
    httpd_register_uri_handler(m_httpServer, &staticFileHandler);

    // Redirecting connection tests to the captive portal, triggering the "login to network" prompt
    m_webServer.onNotFound([softAPURL](AsyncWebServerRequest* request) { request->redirect(softAPURL); });
  } else {
    OS_LOGE(TAG, "/www/index.html or hash files not found, serving error page");

    httpd_register_uri_handler(m_httpServer, &s_errorHandler);
  }

  m_webServer.begin();

  if (fsOk) {
    if (TaskUtils::TaskCreateExpensive(&Util::FnProxy<&CaptivePortalInstance::task>, TAG, 8192, this, 1, &m_taskHandle) != pdPASS) {  // PROFILED: 4-6KB stack usage
      OS_LOGE(TAG, "Failed to create task");
    }
  }
}

CaptivePortalInstance::~CaptivePortalInstance() {
  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }
  m_webServer.end();
  m_socketServer.close();
  m_fileSystem.end();
  m_dnsServer.stop();
}

void CaptivePortalInstance::task() {
  while (true) {
    m_socketServer.loop();
    // instance->m_dnsServer.processNextRequest();
    vTaskDelay(pdMS_TO_TICKS(WEBSOCKET_UPDATE_INTERVAL));
  }
}

void CaptivePortalInstance::handleWebSocketClientConnected(uint8_t socketId) {
  OS_LOGD(TAG, "WebSocket client #%u connected from %s", socketId, m_socketServer.remoteIP(socketId).toString().c_str());

  WiFiNetwork connectedNetwork;
  WiFiNetwork* connectedNetworkPtr = nullptr;
  if (WiFiManager::GetConnectedNetwork(connectedNetwork)) {
    connectedNetworkPtr = &connectedNetwork;
  }

  Serialization::Local::SerializeReadyMessage(connectedNetworkPtr, GatewayConnectionManager::IsLinked(), std::bind(&CaptivePortalInstance::sendMessageBIN, this, socketId, std::placeholders::_1, std::placeholders::_2));

  // Send all previously scanned wifi networks
  auto networks = OpenShock::WiFiManager::GetDiscoveredWiFiNetworks();

  Serialization::Local::SerializeWiFiNetworksEvent(Serialization::Types::WifiNetworkEventType::Discovered, networks, std::bind(&CaptivePortalInstance::sendMessageBIN, this, socketId, std::placeholders::_1, std::placeholders::_2));
}

void CaptivePortalInstance::handleWebSocketClientDisconnected(uint8_t socketId) {
  OS_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

void CaptivePortalInstance::handleWebSocketClientError(uint8_t socketId, uint16_t code, const char* message) {
  OS_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, code, message);
}

void CaptivePortalInstance::handleWebSocketEvent(uint8_t socketId, WebSocketMessageType type, const uint8_t* payload, std::size_t length) {
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
      EventHandlers::WebSocket::HandleLocalBinary(socketId, payload, length);
      break;
    case WebSocketMessageType::Error:
      handleWebSocketClientError(socketId, length, reinterpret_cast<const char*>(payload));
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
