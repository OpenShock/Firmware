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
#include "http/ContentTypes.h"
#include "Logging.h"
#include "RateLimiter.h"
#include "message_handlers/WebSocket.h"
#include "OtaUpdateChannel.h"
#include "serialization/WSLocal.h"
#include "util/FnProxy.h"
#include "util/HexUtils.h"
#include "util/PartitionUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

#include <cJSON.h>
#include <WiFi.h>

const uint16_t HTTP_PORT                 = 80;
const uint16_t WEBSOCKET_PORT            = 81;
const uint16_t DNS_PORT                  = 53;
const uint32_t WEBSOCKET_PING_INTERVAL   = 10'000;
const uint32_t WEBSOCKET_PING_TIMEOUT    = 1000;
const uint8_t WEBSOCKET_PING_RETRIES     = 3;
const uint32_t WEBSOCKET_UPDATE_INTERVAL = 10;  // 10ms / 100Hz

static const char* const JSON_ERR_INTERNAL        = "{\"error\":\"InternalError\"}";
static const char* const JSON_ERR_MISSING_PARAM   = "{\"error\":\"MissingParam\"}";
static const char* const JSON_ERR_INVALID_PIN     = "{\"error\":\"InvalidPin\"}";
static const char* const JSON_ERR_MISSING_SSID    = "{\"error\":\"MissingSsid\"}";
static const char* const JSON_ERR_INVALID_SSID    = "{\"error\":\"InvalidSsid\"}";
static const char* const JSON_ERR_PASSWORD_SHORT  = "{\"error\":\"PasswordTooShort\"}";
static const char* const JSON_ERR_PASSWORD_LONG   = "{\"error\":\"PasswordTooLong\"}";
static const char* const JSON_ERR_CODE_REQUIRED   = "{\"error\":\"CodeRequired\"}";
static const char* const JSON_ERR_INVALID_CHANNEL = "{\"error\":\"InvalidChannel\"}";
static const char* const JSON_ERR_RATE_LIMITED     = "{\"error\":\"RateLimited\"}";

static OpenShock::RateLimiter& getAccountLinkRateLimiter()
{
  static OpenShock::RateLimiter* rl = nullptr;
  if (rl == nullptr) {
    rl = new OpenShock::RateLimiter();
    rl->addLimit(60'000, 5);   // 5 attempts per minute
    rl->addLimit(300'000, 10); // 10 attempts per 5 minutes
  }
  return *rl;
}

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

CaptivePortal::CaptivePortalInstance::CaptivePortalInstance(IPAddress apIP)
  : m_webServer(HTTP_PORT)
  , m_socketServer(WEBSOCKET_PORT, "/ws", "flatbuffers")  // Sec-WebSocket-Protocol = flatbuffers
  , m_socketDeFragger(std::bind(&CaptivePortalInstance::handleWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
  , m_fileSystem()
  , m_dnsServer(apIP)
  , m_taskHandle(nullptr)
{
  m_socketServer.onEvent(std::bind(&WebSocketDeFragger::handler, &m_socketDeFragger, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  m_socketServer.begin();

  m_socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

  OS_LOGI(TAG, "Starting DNS server");
  m_dnsServer.start();

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
    m_webServer.on("/api/board", HTTP_GET, [](AsyncWebServerRequest* request) {
      bool hasPredefinedPins = OPENSHOCK_RF_TX_GPIO != OPENSHOCK_GPIO_INVALID;
      request->send(200, HTTP::ContentType::JSON, hasPredefinedPins ? "{\"has_predefined_pins\":true}" : "{\"has_predefined_pins\":false}");
    });

    m_webServer.on("/api/portal/close", HTTP_POST, [](AsyncWebServerRequest* request) {
      CaptivePortal::SetUserDone();
      request->send(200);
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
      request->send(200);
    });

    m_webServer.on("/api/wifi/networks", HTTP_DELETE, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("ssid")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
        return;
      }
      String ssid = request->getParam("ssid")->value();
      if (!WiFiManager::Forget(ssid.c_str())) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/account/link", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!getAccountLinkRateLimiter().tryRequest()) {
        request->send(429, HTTP::ContentType::JSON, JSON_ERR_RATE_LIMITED);
        return;
      }
      if (!request->hasParam("code")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_CODE_REQUIRED);
        return;
      }
      String code      = request->getParam("code")->value();
      auto result      = GatewayConnectionManager::Link(std::string_view(code.c_str(), code.length()));
      using ResultCode = OpenShock::AccountLinkResultCode;
      if (result == ResultCode::Success) {
        request->send(200);
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
      cJSON* root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "error", error);
      char* json = cJSON_PrintUnformatted(root);
      cJSON_Delete(root);
      if (json == nullptr) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
      } else {
        request->send(400, HTTP::ContentType::JSON, json);
        cJSON_free(json);
      }
    });

    m_webServer.on("/api/account", HTTP_DELETE, [](AsyncWebServerRequest* request) {
      GatewayConnectionManager::UnLink();
      request->send(200);
    });

    m_webServer.on("/api/config/rf/pin", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("pin")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
        return;
      }
      int pin          = request->getParam("pin")->value().toInt();
      auto result      = CommandHandler::SetRfTxPin(static_cast<gpio_num_t>(pin));
      using ResultCode = OpenShock::SetGPIOResultCode;
      if (result != ResultCode::Success) {
        request->send(400, HTTP::ContentType::JSON, (result == ResultCode::InvalidPin) ? JSON_ERR_INVALID_PIN : JSON_ERR_INTERNAL);
        return;
      }
      cJSON* root = cJSON_CreateObject();
      cJSON_AddNumberToObject(root, "pin", pin);
      char* json = cJSON_PrintUnformatted(root);
      cJSON_Delete(root);
      if (json == nullptr) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
      } else {
        request->send(200, HTTP::ContentType::JSON, json);
        cJSON_free(json);
      }
    });

    m_webServer.on("/api/config/estop/pin", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("pin")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
        return;
      }
      int8_t pin = static_cast<int8_t>(request->getParam("pin")->value().toInt());
      if (!IsValidInputPin(pin)) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
        return;
      }
      if (!EStopManager::SetEStopPin(static_cast<gpio_num_t>(pin)) || !Config::SetEStopGpioPin(static_cast<gpio_num_t>(pin))) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cJSON* root = cJSON_CreateObject();
      cJSON_AddNumberToObject(root, "pin", pin);
      char* json = cJSON_PrintUnformatted(root);
      cJSON_Delete(root);
      if (json == nullptr) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
      } else {
        request->send(200, HTTP::ContentType::JSON, json);
        cJSON_free(json);
      }
    });

    m_webServer.on("/api/config/estop/enabled", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("enabled")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      bool enabled = request->getParam("enabled")->value().toInt() != 0;
      bool success = EStopManager::SetEStopEnabled(enabled) && Config::SetEStopEnabled(enabled);
      if (success) {
        request->send(200);
      } else {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
      }
    });

    m_webServer.on("/api/wifi/networks", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("ssid", true)) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
        return;
      }
      String ssid = request->getParam("ssid", true)->value();
      if (ssid.length() == 0 || ssid.length() > 31) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_INVALID_SSID);
        return;
      }
      String password;
      if (request->hasParam("password", true)) {
        password = request->getParam("password", true)->value();
        if (password.length() > 0 && password.length() < 8) {
          request->send(400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_SHORT);
          return;
        }
        if (password.length() > 63) {
          request->send(400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_LONG);
          return;
        }
      }
      bool connect = true;
      if (request->hasParam("connect", true)) {
        connect = request->getParam("connect", true)->value().toInt() != 0;
      }
      wifi_auth_mode_t authMode = WIFI_AUTH_MAX;
      if (request->hasParam("security", true)) {
        int sec = request->getParam("security", true)->value().toInt();
        if (sec >= 0 && sec <= static_cast<int>(WIFI_AUTH_MAX)) {
          authMode = static_cast<wifi_auth_mode_t>(sec);
        }
      }
      if (!WiFiManager::Save(ssid.c_str(), std::string_view(password.c_str(), password.length()), connect, authMode)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("ssid", true)) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
        return;
      }
      String ssid = request->getParam("ssid", true)->value();
      if (!WiFiManager::Connect(ssid.c_str())) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/wifi/disconnect", HTTP_POST, [](AsyncWebServerRequest* request) {
      WiFiManager::Disconnect();
      request->send(200);
    });

    m_webServer.on("/api/ota/enabled", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("enabled")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
        return;
      }
      bool enabled = request->getParam("enabled")->value().toInt() != 0;
      Config::OtaUpdateConfig cfg;
      if (!Config::GetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cfg.isEnabled = enabled;
      if (!Config::SetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/ota/domain", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("domain")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
        return;
      }
      String domain = request->getParam("domain")->value();
      Config::OtaUpdateConfig cfg;
      if (!Config::GetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cfg.cdnDomain = std::string(domain.c_str(), domain.length());
      if (!Config::SetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/ota/channel", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("channel")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
        return;
      }
      String channelStr = request->getParam("channel")->value();
      OtaUpdateChannel channel;
      if (!TryParseOtaUpdateChannel(channel, channelStr.c_str())) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_INVALID_CHANNEL);
        return;
      }
      Config::OtaUpdateConfig cfg;
      if (!Config::GetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cfg.updateChannel = channel;
      if (!Config::SetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/ota/check-interval", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("interval")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
        return;
      }
      uint16_t interval = static_cast<uint16_t>(request->getParam("interval")->value().toInt());
      Config::OtaUpdateConfig cfg;
      if (!Config::GetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cfg.checkInterval = interval;
      if (!Config::SetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/ota/allow-backend-management", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("allow")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
        return;
      }
      bool allow = request->getParam("allow")->value().toInt() != 0;
      Config::OtaUpdateConfig cfg;
      if (!Config::GetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cfg.allowBackendManagement = allow;
      if (!Config::SetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/ota/require-manual-approval", HTTP_PUT, [](AsyncWebServerRequest* request) {
      if (!request->hasParam("require")) {
        request->send(400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
        return;
      }
      bool require = request->getParam("require")->value().toInt() != 0;
      Config::OtaUpdateConfig cfg;
      if (!Config::GetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      cfg.requireManualApproval = require;
      if (!Config::SetOtaUpdateConfig(cfg)) {
        request->send(500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
        return;
      }
      request->send(200);
    });

    m_webServer.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest* request) {
      // TODO: trigger OTA check - OtaUpdateManager does not yet expose a CheckForUpdates method
      request->send(200);
    });

    // Serving the captive portal files from LittleFS
    m_webServer.serveStatic("/", m_fileSystem, "/www/", "max-age=3600").setDefaultFile("index.html").setSharedEtag(fsHash);

    m_webServer.onNotFound(&RFC8908Handler::CatchAll);

  } else {
    OS_LOGE(TAG, "/www/index.html or hash files not found, serving error page");

    m_webServer.onNotFound([](AsyncWebServerRequest* request) {
      request->send(
        200,
        HTTP::ContentType::TextPlain,
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
    m_stopRequested.store(false, std::memory_order_relaxed);
    if (TaskUtils::TaskCreateExpensive(Util::FnProxy<&CaptivePortal::CaptivePortalInstance::task>, TAG, 8192, this, 1, &m_taskHandle) != pdPASS) {  // PROFILED: 4-6KB stack usage
      OS_LOGE(TAG, "Failed to create task");
    }
  }
}

CaptivePortal::CaptivePortalInstance::~CaptivePortalInstance()
{
  m_stopRequested.store(true, std::memory_order_relaxed);
  if (m_taskHandle != nullptr) {
    TaskUtils::StopTask(m_taskHandle, TAG, "CaptivePortal task");
    m_taskHandle = nullptr;
  }
  m_webServer.end();
  m_socketServer.close();
  m_fileSystem.end();
  m_dnsServer.stop();
}

void CaptivePortal::CaptivePortalInstance::task()
{
  while (!m_stopRequested.load(std::memory_order_relaxed)) {
    m_socketServer.loop();
    vTaskDelay(pdMS_TO_TICKS(WEBSOCKET_UPDATE_INTERVAL));
  }
  vTaskDelete(nullptr);
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
