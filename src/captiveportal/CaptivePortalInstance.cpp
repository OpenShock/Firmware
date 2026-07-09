#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "captiveportal/CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "captiveportal/Manager.h"
#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "enums/OtaUpdateChannel.h"
#include "estop/EStopManager.h"
#include "GatewayConnectionManager.h"
#include "http/ContentTypes.h"
#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "RateLimiter.h"
#include "rfc8908/RFC8908Handler.h"
#include "serialization/WSLocal.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

#include "json/Json.h"

#include <cctype>
#include <cstring>
#include <functional>
#include <string>

const uint16_t HTTP_PORT = 80;

// Largest inbound WebSocket message we'll accept (local FlatBuffer commands are tiny).
static constexpr size_t MAX_WS_MSG = 8 * 1024;

// HTTP status lines (esp_http_server needs the full "code reason" string, and stores
// the pointer rather than copying — so these must have static storage duration).
static constexpr const char* S200 = "200 OK";
static constexpr const char* S304 = "304 Not Modified";
static constexpr const char* S400 = "400 Bad Request";
static constexpr const char* S429 = "429 Too Many Requests";
static constexpr const char* S500 = "500 Internal Server Error";

static const char* const JSON_ERR_INTERNAL        = "{\"error\":\"InternalError\"}";
static const char* const JSON_ERR_MISSING_PARAM   = "{\"error\":\"MissingParam\"}";
static const char* const JSON_ERR_INVALID_PIN     = "{\"error\":\"InvalidPin\"}";
static const char* const JSON_ERR_MISSING_SSID    = "{\"error\":\"MissingSsid\"}";
static const char* const JSON_ERR_INVALID_SSID    = "{\"error\":\"InvalidSsid\"}";
static const char* const JSON_ERR_PASSWORD_SHORT  = "{\"error\":\"PasswordTooShort\"}";
static const char* const JSON_ERR_PASSWORD_LONG   = "{\"error\":\"PasswordTooLong\"}";
static const char* const JSON_ERR_CODE_REQUIRED   = "{\"error\":\"CodeRequired\"}";
static const char* const JSON_ERR_INVALID_CHANNEL = "{\"error\":\"InvalidChannel\"}";
static const char* const JSON_ERR_RATE_LIMITED    = "{\"error\":\"RateLimited\"}";

using namespace OpenShock;

static OpenShock::RateLimiter& getAccountLinkRateLimiter()
{
  static OpenShock::RateLimiter* rl = nullptr;
  if (rl == nullptr) {
    rl = new OpenShock::RateLimiter();
    rl->addLimit(60'000, 5);    // 5 attempts per minute
    rl->addLimit(300'000, 10);  // 10 attempts per 5 minutes
  }
  return *rl;
}

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

// ---------------------------------------------------------------------------
// Small HTTP helpers (replace the ESPAsyncWebServer request API)
// ---------------------------------------------------------------------------

static esp_err_t sendResp(httpd_req_t* req, const char* status, const char* type, std::string_view body)
{
  httpd_resp_set_status(req, status);
  if (type != nullptr) {
    httpd_resp_set_type(req, type);
  }
  return httpd_resp_send(req, body.data(), body.size());
}

static std::string urlDecode(const char* s)
{
  auto hexVal = [](char h) -> int {
    if (h >= '0' && h <= '9') return h - '0';
    h = static_cast<char>(h | 0x20);
    return h - 'a' + 10;
  };

  std::string out;
  for (size_t i = 0; s[i] != '\0'; ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && isxdigit(static_cast<unsigned char>(s[i + 1])) && isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>((hexVal(s[i + 1]) << 4) | hexVal(s[i + 2]));
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

// Read and URL-decode a query-string parameter. httpd does not decode for us.
static bool getQueryParam(httpd_req_t* req, const char* key, std::string& out)
{
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) {
    return false;
  }

  std::string query;
  query.resize(qlen + 1);
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
    return false;
  }

  char val[256];
  if (httpd_query_key_value(query.c_str(), key, val, sizeof(val)) != ESP_OK) {
    return false;
  }

  out = urlDecode(val);
  return true;
}

// Read a urlencoded request body (application/x-www-form-urlencoded).
static bool recvBody(httpd_req_t* req, std::string& out)
{
  int total = req->content_len;
  if (total <= 0 || total > 4096) {
    return false;
  }

  out.resize(total);
  int off = 0;
  while (off < total) {
    int r = httpd_req_recv(req, out.data() + off, total - off);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (r <= 0) {
      return false;
    }
    off += r;
  }
  return true;
}

static bool getFormParam(const std::string& body, const char* key, std::string& out)
{
  char val[256];
  if (httpd_query_key_value(body.c_str(), key, val, sizeof(val)) != ESP_OK) {
    return false;
  }
  out = urlDecode(val);
  return true;
}

static const char* contentTypeForPath(const std::string& path)
{
  auto endsWith = [&](const char* ext) {
    size_t n = strlen(ext);
    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
  };

  if (endsWith(".html")) return HTTP::ContentType::TextHTML;
  if (endsWith(".js")) return "text/javascript";
  if (endsWith(".css")) return "text/css";
  if (endsWith(".svg")) return "image/svg+xml";
  if (endsWith(".png")) return "image/png";
  if (endsWith(".ico")) return "image/x-icon";
  if (endsWith(".json")) return HTTP::ContentType::JSON;
  if (endsWith(".woff2")) return "font/woff2";
  if (endsWith(".txt")) return HTTP::ContentType::TextPlain;
  return "application/octet-stream";
}

// ---------------------------------------------------------------------------
// API handlers (ported 1:1 from the ESPAsyncWebServer lambdas)
// ---------------------------------------------------------------------------

static esp_err_t apiBoard(httpd_req_t* req)
{
  bool hasPredefinedPins = OPENSHOCK_RF_TX_GPIO != OPENSHOCK_GPIO_INVALID;
  return sendResp(req, S200, HTTP::ContentType::JSON, hasPredefinedPins ? "{\"has_predefined_pins\":true}" : "{\"has_predefined_pins\":false}");
}

static esp_err_t apiPortalClose(httpd_req_t* req)
{
  CaptivePortal::SetUserDone();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiScan(httpd_req_t* req)
{
  bool run = true;
  std::string runStr;
  if (getQueryParam(req, "run", runStr)) {
    run = atoi(runStr.c_str()) != 0;
  }
  if (run) {
    WiFiScanManager::StartScan();
  } else {
    WiFiScanManager::AbortScan();
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiNetworksDelete(httpd_req_t* req)
{
  std::string ssid;
  if (!getQueryParam(req, "ssid", ssid)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  if (!WiFiManager::Forget(ssid.c_str())) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiAccountLink(httpd_req_t* req)
{
  if (!getAccountLinkRateLimiter().tryRequest()) {
    return sendResp(req, S429, HTTP::ContentType::JSON, JSON_ERR_RATE_LIMITED);
  }
  std::string code;
  if (!getQueryParam(req, "code", code)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_CODE_REQUIRED);
  }
  auto result      = GatewayConnectionManager::Link(std::string_view(code));
  using ResultCode = OpenShock::AccountLinkResultCode;
  if (result == ResultCode::Success) {
    return sendResp(req, S200, nullptr, {});
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
    case ResultCode::RequestFailed:
      error = "RequestFailed";
      break;
    case ResultCode::RequestTimedOut:
      error = "RequestTimedOut";
      break;
    case ResultCode::ServerError:
      error = "ServerError";
      break;
    case ResultCode::InvalidResponse:
      error = "InvalidResponse";
      break;
    case ResultCode::ConfigSaveFailed:
      error = "ConfigSaveFailed";
      break;
    default:
      error = "InternalError";
      break;
  }
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  OpenShock::JSON::objSetString(gen, "error", error);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S400, HTTP::ContentType::JSON, json);
}

static esp_err_t apiAccountDelete(httpd_req_t* req)
{
  GatewayConnectionManager::UnLink();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiConfigRfPin(httpd_req_t* req)
{
  std::string pinStr;
  if (!getQueryParam(req, "pin", pinStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  int pin          = atoi(pinStr.c_str());
  auto result      = CommandHandler::SetRfTxPin(static_cast<gpio_num_t>(pin));
  using ResultCode = OpenShock::SetGPIOResultCode;
  if (result != ResultCode::Success) {
    return sendResp(req, S400, HTTP::ContentType::JSON, (result == ResultCode::InvalidPin) ? JSON_ERR_INVALID_PIN : JSON_ERR_INTERNAL);
  }
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  json_gen_obj_set_int(gen, "pin", pin);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S200, HTTP::ContentType::JSON, json);
}

static esp_err_t apiConfigEstopPin(httpd_req_t* req)
{
  std::string pinStr;
  if (!getQueryParam(req, "pin", pinStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  int8_t pin = static_cast<int8_t>(atoi(pinStr.c_str()));
  if (!IsValidInputPin(pin)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  if (!EStopManager::SetEStopPin(static_cast<gpio_num_t>(pin)) || !Config::SetEStopGpioPin(static_cast<gpio_num_t>(pin))) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  json_gen_obj_set_int(gen, "pin", pin);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S200, HTTP::ContentType::JSON, json);
}

static esp_err_t apiConfigEstopEnabled(httpd_req_t* req)
{
  std::string enabledStr;
  if (!getQueryParam(req, "enabled", enabledStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  bool enabled = atoi(enabledStr.c_str()) != 0;
  bool success = EStopManager::SetEStopEnabled(enabled) && Config::SetEStopEnabled(enabled);
  if (success) {
    return sendResp(req, S200, nullptr, {});
  }
  return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
}

static esp_err_t apiWifiNetworksAdd(httpd_req_t* req)
{
  std::string body;
  if (!recvBody(req, body)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }

  std::string ssid;
  if (!getFormParam(body, "ssid", ssid) || ssid.empty() || ssid.length() > 31) {
    return sendResp(req, S400, HTTP::ContentType::JSON, ssid.empty() ? JSON_ERR_MISSING_SSID : JSON_ERR_INVALID_SSID);
  }

  std::string password;
  if (getFormParam(body, "password", password) && !password.empty()) {
    if (password.length() < 8) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_SHORT);
    }
    if (password.length() > 63) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_LONG);
    }
  }

  bool connect = true;
  std::string connectStr;
  if (getFormParam(body, "connect", connectStr)) {
    connect = atoi(connectStr.c_str()) != 0;
  }

  wifi_auth_mode_t authMode = WIFI_AUTH_MAX;
  std::string securityStr;
  if (getFormParam(body, "security", securityStr)) {
    int sec = atoi(securityStr.c_str());
    if (sec >= 0 && sec <= static_cast<int>(WIFI_AUTH_MAX)) {
      authMode = static_cast<wifi_auth_mode_t>(sec);
    }
  }

  if (!WiFiManager::Save(ssid.c_str(), std::string_view(password), connect, authMode)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiConnect(httpd_req_t* req)
{
  std::string body;
  std::string ssid;
  if (!recvBody(req, body) || !getFormParam(body, "ssid", ssid)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  if (!WiFiManager::Connect(ssid.c_str())) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiDisconnect(httpd_req_t* req)
{
  WiFiManager::Disconnect();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaEnabled(httpd_req_t* req)
{
  std::string enabledStr;
  if (!getQueryParam(req, "enabled", enabledStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }
  bool enabled = atoi(enabledStr.c_str()) != 0;
  Config::OtaUpdateConfig cfg;
  if (!Config::GetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  cfg.isEnabled = enabled;
  if (!Config::SetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaDomain(httpd_req_t* req)
{
  std::string domain;
  if (!getQueryParam(req, "domain", domain)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }
  Config::OtaUpdateConfig cfg;
  if (!Config::GetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  cfg.cdnDomain = domain;
  if (!Config::SetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaChannel(httpd_req_t* req)
{
  std::string channelStr;
  if (!getQueryParam(req, "channel", channelStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }
  OtaUpdateChannel channel;
  if (!TryParseOtaUpdateChannel(channel, channelStr.c_str())) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_CHANNEL);
  }
  Config::OtaUpdateConfig cfg;
  if (!Config::GetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  cfg.updateChannel = channel;
  if (!Config::SetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaCheckInterval(httpd_req_t* req)
{
  std::string intervalStr;
  if (!getQueryParam(req, "interval", intervalStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }
  uint16_t interval = static_cast<uint16_t>(atoi(intervalStr.c_str()));
  Config::OtaUpdateConfig cfg;
  if (!Config::GetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  cfg.checkInterval = interval;
  if (!Config::SetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaAllowBackendManagement(httpd_req_t* req)
{
  std::string allowStr;
  if (!getQueryParam(req, "allow", allowStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }
  bool allow = atoi(allowStr.c_str()) != 0;
  Config::OtaUpdateConfig cfg;
  if (!Config::GetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  cfg.allowBackendManagement = allow;
  if (!Config::SetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaRequireManualApproval(httpd_req_t* req)
{
  std::string requireStr;
  if (!getQueryParam(req, "require", requireStr)) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }
  bool require = atoi(requireStr.c_str()) != 0;
  Config::OtaUpdateConfig cfg;
  if (!Config::GetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  cfg.requireManualApproval = require;
  if (!Config::SetOtaUpdateConfig(cfg)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaCheck(httpd_req_t* req)
{
  // TODO: trigger OTA check - OtaUpdateManager does not yet expose a CheckForUpdates method
  return sendResp(req, S200, nullptr, {});
}

// ---------------------------------------------------------------------------
// Static file serving (gzipped assets from the raw littlefs static0 partition)
// ---------------------------------------------------------------------------

esp_err_t CaptivePortal::CaptivePortalInstance::staticFileHandler(httpd_req_t* req)
{
  auto* self = static_cast<CaptivePortalInstance*>(req->user_ctx);

  if (!self->m_staticFs.isMounted()) {
    // Filesystem image was never uploaded — serve the help page for any request.
    return sendResp(
      req,
      S200,
      HTTP::ContentType::TextPlain,
      // Raw string literal (1+ to remove the first newline)
      1 + R"(
You probably forgot to upload the Filesystem with PlatformIO!
Go to PlatformIO -> Platform -> Upload Filesystem Image!
If this happened with a file we provided or you just need help, come to the Discord!

discord.gg/OpenShock
)"
    );
  }

  std::string uri(req->uri);
  auto qpos = uri.find('?');
  if (qpos != std::string::npos) {
    uri.resize(qpos);
  }

  // Reject path traversal, then fall through to the captive redirect.
  if (uri.find("..") != std::string::npos) {
    return RFC8908::EmitRedirect(req);
  }

  if (uri == "/") {
    uri = "/index.html";
  }

  // All assets are pre-gzipped, stored at /www/<path>.gz on the static0 partition.
  std::string path = "/www" + uri + ".gz";

  if (!self->m_staticFs.exists(path.c_str())) {
    // Unknown path → captive redirect (matches serveStatic default-file fallthrough).
    return RFC8908::EmitRedirect(req);
  }

  // ETag / conditional request. Keep the quoted hash alive until after send.
  std::string etag;
  if (self->m_fsHash != nullptr) {
    etag = std::string("\"") + self->m_fsHash + "\"";
    char inm[80];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && etag == inm) {
      httpd_resp_set_status(req, S304);
      return httpd_resp_send(req, nullptr, 0);
    }
  }

  httpd_resp_set_type(req, contentTypeForPath(uri));
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
  if (!etag.empty()) {
    httpd_resp_set_hdr(req, "ETag", etag.c_str());
  }

  bool ok = self->m_staticFs.readFile(path.c_str(), [req](std::span<const uint8_t> chunk) { return httpd_resp_send_chunk(req, reinterpret_cast<const char*>(chunk.data()), chunk.size()) == ESP_OK; });
  if (!ok) {
    // Headers/chunks may already be on the wire — can't cleanly redirect now.
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

namespace {
  // Per-session context so httpd's free callback can reap the fd->id slot on close.
  struct WsSessCtx {
    OpenShock::CaptivePortal::CaptivePortalInstance* self;
    int fd;
  };

  // Deferred WS send marshalled onto the httpd task via httpd_queue_work.
  struct WsSendJob {
    httpd_handle_t hd;
    int fd;
    bool broadcast;
    bool binary;
    std::vector<uint8_t> data;
  };

  void wsSendWork(void* arg)
  {
    auto* job = static_cast<WsSendJob*>(arg);

    httpd_ws_frame_t frame = {};
    frame.final            = true;
    frame.type             = job->binary ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT;
    frame.payload          = job->data.data();
    frame.len              = job->data.size();

    if (job->broadcast) {
      size_t count = 8;
      int fds[8];
      if (httpd_get_client_list(job->hd, &count, fds) == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
          if (httpd_ws_get_fd_info(job->hd, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(job->hd, fds[i], &frame);
          }
        }
      }
    } else {
      httpd_ws_send_frame_async(job->hd, job->fd, &frame);
    }

    delete job;
  }
}  // namespace

int CaptivePortal::CaptivePortalInstance::idForFd(int fd)
{
  ScopedLock lock__(&m_clientsMutex);
  for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (m_clients[i].used && m_clients[i].fd == fd) {
      return i;
    }
  }
  return -1;
}

int CaptivePortal::CaptivePortalInstance::fdForId(uint8_t socketId)
{
  ScopedLock lock__(&m_clientsMutex);
  if (socketId < MAX_WS_CLIENTS && m_clients[socketId].used) {
    return m_clients[socketId].fd;
  }
  return -1;
}

uint8_t CaptivePortal::CaptivePortalInstance::onWsOpen(int fd)
{
  ScopedLock lock__(&m_clientsMutex);

  // Idempotent: an fd already tracked keeps its id.
  for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (m_clients[i].used && m_clients[i].fd == fd) {
      return i;
    }
  }

  for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (!m_clients[i].used) {
      m_clients[i].used      = true;
      m_clients[i].fd        = fd;
      m_clients[i].reasmType = WebSocketMessageType::Binary;
      m_clients[i].reasm.clear();
      return i;
    }
  }

  return 0xFF;  // full
}

void CaptivePortal::CaptivePortalInstance::onWsClose(int fd)
{
  int id = -1;
  {
    ScopedLock lock__(&m_clientsMutex);
    for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
      if (m_clients[i].used && m_clients[i].fd == fd) {
        m_clients[i].used = false;
        m_clients[i].reasm.clear();
        id = i;
        break;
      }
    }
  }

  if (id >= 0) {
    handleWebSocketClientDisconnected(static_cast<uint8_t>(id));
  }
}

void CaptivePortal::CaptivePortalInstance::wsSessCtxFree(void* ctx)
{
  auto* c = static_cast<WsSessCtx*>(ctx);
  if (c == nullptr) {
    return;
  }
  c->self->onWsClose(c->fd);
  delete c;
}

esp_err_t CaptivePortal::CaptivePortalInstance::wsHandler(httpd_req_t* req)
{
  auto* self = static_cast<CaptivePortalInstance*>(req->user_ctx);
  int fd     = httpd_req_to_sockfd(req);

  // Initial GET = handshake just completed → allocate id, arm disconnect hook, greet.
  if (req->method == HTTP_GET) {
    uint8_t id = self->onWsOpen(fd);
    if (id == 0xFF) {
      OS_LOGW(TAG, "WebSocket client table full, rejecting fd %d", fd);
      httpd_sess_trigger_close(self->m_server, fd);
      return ESP_OK;
    }

    auto* sessCtx = new WsSessCtx {self, fd};
    httpd_sess_set_ctx(self->m_server, fd, sessCtx, &CaptivePortalInstance::wsSessCtxFree);

    self->handleWebSocketClientConnected(req, id);
    return ESP_OK;
  }

  // Data frame: two-call recv (length first, then payload).
  httpd_ws_frame_t frame = {};
  esp_err_t ret          = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  if (frame.len == 0) {
    self->onWsFrame(fd, frame.type, frame.final, {});
    return ESP_OK;
  }

  if (frame.len > MAX_WS_MSG) {
    OS_LOGE(TAG, "WebSocket frame too large (%u bytes), dropping", static_cast<unsigned>(frame.len));
    return ESP_FAIL;
  }

  std::vector<uint8_t> buf(frame.len);
  frame.payload = buf.data();
  ret           = httpd_ws_recv_frame(req, &frame, frame.len);
  if (ret != ESP_OK) {
    return ret;
  }

  self->onWsFrame(fd, frame.type, frame.final, {buf.data(), frame.len});
  return ESP_OK;
}

void CaptivePortal::CaptivePortalInstance::onWsFrame(int fd, httpd_ws_type_t opcode, bool final, std::span<const uint8_t> payload)
{
  int idx = idForFd(fd);
  if (idx < 0) {
    // Lazy fallback in case the handshake GET call didn't fire on this IDF build.
    uint8_t id = onWsOpen(fd);
    if (id == 0xFF) {
      return;
    }
    idx = id;
  }
  uint8_t socketId = static_cast<uint8_t>(idx);

  switch (opcode) {
    case HTTPD_WS_TYPE_TEXT:
    case HTTPD_WS_TYPE_BINARY:
    {
      WebSocketMessageType type = (opcode == HTTPD_WS_TYPE_TEXT) ? WebSocketMessageType::Text : WebSocketMessageType::Binary;
      if (final) {
        dispatchWsMessage(socketId, type, payload);
      } else {
        ScopedLock lock__(&m_clientsMutex);
        m_clients[idx].reasmType = type;
        m_clients[idx].reasm.assign(payload.begin(), payload.end());
      }
      break;
    }
    case HTTPD_WS_TYPE_CONTINUE:
    {
      WebSocketMessageType type;
      std::vector<uint8_t> full;
      {
        ScopedLock lock__(&m_clientsMutex);
        m_clients[idx].reasm.insert(m_clients[idx].reasm.end(), payload.begin(), payload.end());
        if (!final) {
          break;
        }
        type = m_clients[idx].reasmType;
        full = std::move(m_clients[idx].reasm);
        m_clients[idx].reasm.clear();
      }
      dispatchWsMessage(socketId, type, full);
      break;
    }
    default:
      // PING/PONG/CLOSE are handled by httpd (handle_ws_control_frames = false).
      break;
  }
}

void CaptivePortal::CaptivePortalInstance::dispatchWsMessage(uint8_t socketId, WebSocketMessageType type, std::span<const uint8_t> payload)
{
  switch (type) {
    case WebSocketMessageType::Binary:
      MessageHandlers::WebSocket::HandleLocalBinary(socketId, payload);
      break;
    case WebSocketMessageType::Text:
      OS_LOGE(TAG, "Text messages are not supported");
      break;
    default:
      break;
  }
}

void CaptivePortal::CaptivePortalInstance::handleWebSocketClientConnected(httpd_req_t* req, uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u connected (fd %d)", socketId, httpd_req_to_sockfd(req));

  // We're on the httpd task with a live req — send directly, no copy/queue needed.
  auto sendBin = [req](std::span<const uint8_t> data) -> bool {
    httpd_ws_frame_t frame = {};
    frame.final            = true;
    frame.type             = HTTPD_WS_TYPE_BINARY;
    frame.payload          = const_cast<uint8_t*>(data.data());
    frame.len              = data.size();
    return httpd_ws_send_frame(req, &frame) == ESP_OK;
  };

  WiFiNetwork connectedNetwork;
  WiFiNetwork* connectedNetworkPtr = nullptr;
  if (WiFiManager::GetConnectedNetwork(connectedNetwork)) {
    connectedNetworkPtr = &connectedNetwork;
  }

  Serialization::Local::SerializeReadyMessage(connectedNetworkPtr, GatewayConnectionManager::IsLinked(), sendBin);

  // Send all previously scanned wifi networks
  auto networks = OpenShock::WiFiManager::GetDiscoveredWiFiNetworks();
  Serialization::Local::SerializeWiFiNetworksEvent(Serialization::Types::WifiNetworkEventType::Discovered, networks, sendBin);
}

void CaptivePortal::CaptivePortalInstance::handleWebSocketClientDisconnected(uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

// ---------------------------------------------------------------------------
// Public WS send API (called from arbitrary tasks → must copy + queue)
// ---------------------------------------------------------------------------

bool CaptivePortal::CaptivePortalInstance::queueSend(int fd, bool broadcast, bool binary, std::span<const uint8_t> data)
{
  if (m_server == nullptr) {
    return false;
  }

  auto* job      = new WsSendJob;
  job->hd        = m_server;
  job->fd        = fd;
  job->broadcast = broadcast;
  job->binary    = binary;
  job->data.assign(data.begin(), data.end());

  if (httpd_queue_work(m_server, wsSendWork, job) != ESP_OK) {
    delete job;
    return false;
  }
  return true;
}

bool CaptivePortal::CaptivePortalInstance::sendMessageTXT(uint8_t socketId, std::string_view data)
{
  int fd = fdForId(socketId);
  if (fd < 0) {
    return false;
  }
  return queueSend(fd, false, false, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

bool CaptivePortal::CaptivePortalInstance::sendMessageBIN(uint8_t socketId, std::span<const uint8_t> data)
{
  int fd = fdForId(socketId);
  if (fd < 0) {
    return false;
  }
  return queueSend(fd, false, true, data);
}

bool CaptivePortal::CaptivePortalInstance::broadcastMessageTXT(std::string_view data)
{
  return queueSend(-1, true, false, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

bool CaptivePortal::CaptivePortalInstance::broadcastMessageBIN(std::span<const uint8_t> data)
{
  return queueSend(-1, true, true, data);
}

bool CaptivePortal::CaptivePortalInstance::hasClients()
{
  ScopedLock lock__(&m_clientsMutex);
  for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (m_clients[i].used) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// HTTP server setup
// ---------------------------------------------------------------------------

void CaptivePortal::CaptivePortalInstance::registerHandlers()
{
  auto reg = [this](const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t def = {};
    def.uri         = uri;
    def.method      = method;
    def.handler     = handler;
    def.user_ctx    = this;
    esp_err_t err   = httpd_register_uri_handler(m_server, &def);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to register handler %s: %s", uri, esp_err_to_name(err));
    }
  };

  // API endpoints (registered before the static wildcard so they take priority).
  reg("/api/board", HTTP_GET, apiBoard);
  reg("/api/portal/close", HTTP_POST, apiPortalClose);
  reg("/api/wifi/scan", HTTP_POST, apiWifiScan);
  reg("/api/wifi/networks", HTTP_DELETE, apiWifiNetworksDelete);
  reg("/api/wifi/networks", HTTP_POST, apiWifiNetworksAdd);
  reg("/api/wifi/connect", HTTP_POST, apiWifiConnect);
  reg("/api/wifi/disconnect", HTTP_POST, apiWifiDisconnect);
  reg("/api/account/link", HTTP_POST, apiAccountLink);
  reg("/api/account", HTTP_DELETE, apiAccountDelete);
  reg("/api/config/rf/pin", HTTP_PUT, apiConfigRfPin);
  reg("/api/config/estop/pin", HTTP_PUT, apiConfigEstopPin);
  reg("/api/config/estop/enabled", HTTP_PUT, apiConfigEstopEnabled);
  reg("/api/ota/enabled", HTTP_PUT, apiOtaEnabled);
  reg("/api/ota/domain", HTTP_PUT, apiOtaDomain);
  reg("/api/ota/channel", HTTP_PUT, apiOtaChannel);
  reg("/api/ota/check-interval", HTTP_PUT, apiOtaCheckInterval);
  reg("/api/ota/allow-backend-management", HTTP_PUT, apiOtaAllowBackendManagement);
  reg("/api/ota/require-manual-approval", HTTP_PUT, apiOtaRequireManualApproval);
  reg("/api/ota/check", HTTP_POST, apiOtaCheck);

  // OS captive-detection probes + RFC 8908 endpoint + 404 redirect (rfc8908 component).
  // Registered before the static wildcard so the specific probe paths take priority.
  RFC8908::RegisterProbeHandlers(m_server, CaptivePortal::ApIPv4String());

  // WebSocket.
  httpd_uri_t ws              = {};
  ws.uri                      = "/ws";
  ws.method                   = HTTP_GET;
  ws.handler                  = &CaptivePortalInstance::wsHandler;
  ws.user_ctx                 = this;
  ws.is_websocket             = true;
  ws.handle_ws_control_frames = false;
  ws.supported_subprotocol    = "flatbuffers";
  if (httpd_register_uri_handler(m_server, &ws) != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WebSocket handler");
  }

  // Static files — catch-all, registered LAST so specific routes win.
  reg("/*", HTTP_GET, &CaptivePortalInstance::staticFileHandler);
}

bool CaptivePortal::CaptivePortalInstance::startHttpServer()
{
  httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
  config.server_port       = HTTP_PORT;
  config.max_uri_handlers  = 40;
  config.max_open_sockets  = 4;
  config.lru_purge_enable  = true;
  config.stack_size        = 8192;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  config.uri_match_fn      = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&m_server, &config);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    m_server = nullptr;
    return false;
  }

  registerHandlers();
  return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CaptivePortal::CaptivePortalInstance::CaptivePortalInstance()
  : m_server(nullptr)
  , m_staticFs()
  , m_fsHash(nullptr)
  , m_dnsServer()
  , m_clients {}
  , m_clientsMutex()
{
  // Mount the static filesystem (gzipped portal assets) read-only via raw littlefs.
  const esp_partition_t* partition = getStaticPartition();
  if (partition == nullptr) {
    OS_LOGE(TAG, "Failed to find static filesystem partition");
  } else if (!m_staticFs.mount(partition)) {
    OS_LOGE(TAG, "Failed to mount static filesystem");
  } else if (!m_staticFs.exists("/www/index.html.gz")) {
    OS_LOGE(TAG, "/www/index.html.gz not found — serving error page");
  } else {
    m_fsHash = getPartitionHash();
    OS_LOGI(TAG, "Serving files from littlefs (hash: %s)", m_fsHash != nullptr ? m_fsHash : "?");
  }

  // Start the combined HTTP + WebSocket server on port 80.
  if (!startHttpServer()) {
    return;
  }

  // Start the wildcard DNS responder (all A queries → the portal AP IP).
  m_dnsServer.start(CaptivePortal::ApIPv4String());
}

CaptivePortal::CaptivePortalInstance::~CaptivePortalInstance()
{
  m_dnsServer.stop();

  // Stop the server (closes all WS sockets, firing the session free callbacks).
  if (m_server != nullptr) {
    httpd_stop(m_server);
    m_server = nullptr;
  }

  m_staticFs.unmount();
}
