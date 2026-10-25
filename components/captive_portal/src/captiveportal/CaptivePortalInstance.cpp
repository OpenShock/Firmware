#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "captiveportal/CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "AppHooks.h"
#include "BoardDescriptor.h"
#include "captiveportal/Manager.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "Convert.h"
#include "enums/OtaUpdateChannel.h"
#include "GatewayConnectionManager.h"
#include "http/ContentTypes.h"
#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "RateLimiter.h"
#include "rfc8908/RFC8908Handler.h"
#include "serialization/WSLocal.h"
#include "util/HexUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include "json/Json.h"

#include <atomic>
#include <cstring>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

const uint16_t HTTP_PORT = 80;

// Largest inbound WebSocket message we'll accept (local FlatBuffer commands are tiny).
// Nothing larger is accepted by the local message handler, so don't buffer more than that.
static constexpr size_t MAX_WS_MSG = OpenShock::MessageHandlers::WebSocket::MaxLocalMessageSize;

// HTTP status lines (esp_http_server needs the full "code reason" string, and stores
// the pointer rather than copying, so these must have static storage duration).
static constexpr const char* S200 = "200 OK";
static constexpr const char* S304 = "304 Not Modified";
static constexpr const char* S400 = "400 Bad Request";
static constexpr const char* S429 = "429 Too Many Requests";
static constexpr const char* S500 = "500 Internal Server Error";
static constexpr const char* S503 = "503 Service Unavailable";

static const char* const JSON_ERR_INTERNAL        = "{\"error\":\"InternalError\"}";
static const char* const JSON_ERR_MISSING_PARAM   = "{\"error\":\"MissingParam\"}";
static const char* const JSON_ERR_INVALID_PARAM   = "{\"error\":\"InvalidParam\"}";
static const char* const JSON_ERR_INVALID_PIN     = "{\"error\":\"InvalidPin\"}";
static const char* const JSON_ERR_PIN_IN_USE      = "{\"error\":\"PinInUse\"}";
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
  // Only called from the httpd task
  static OpenShock::RateLimiter* rl = nullptr;
  if (rl == nullptr) {
    rl = new OpenShock::RateLimiter();
    rl->addLimit(60'000, 5);    // 5 attempts per minute
    rl->addLimit(300'000, 10);  // 10 attempts per 5 minutes
  }
  return *rl;
}

static const char* getPartitionHash()
{
  const esp_partition_t* partition = FindStaticPartition();
  if (partition == nullptr) {
    return nullptr;
  }

  // Hashing the whole partition takes a noticeable time and its content only changes through OTA, which restarts the
  // device, so compute it once per boot. Only called from the portal manager task.
  static char hash[65];
  static bool hashValid = false;
  if (!hashValid) {
    hashValid = OpenShock::TryGetPartitionHash(partition, hash);
  }

  return hashValid ? hash : nullptr;
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

enum class ParamError {
  Missing,
  Invalid,  // present, but not decodable (an encoded NUL) or not a valid value
};

// Fails with ParamError::Invalid for an encoded NUL ("%00"), which would silently truncate the value at every c_str() use.
[[nodiscard]] static std::expected<std::string, ParamError> urlDecode(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
      continue;
    }
    // A '%' without two characters after it is kept as-is.
    if (c == '%' && i + 2 < s.size()) {
      if (auto decoded = HexUtils::TryParseHexPair(s[i + 1], s[i + 2])) {
        if (*decoded == 0) {
          return std::unexpected(ParamError::Invalid);
        }
        out += static_cast<char>(*decoded);
        i += 2;
        continue;
      }
    }
    out += c;
  }
  return out;
}

// Read and URL-decode a query-string parameter. httpd does not decode for us.
[[nodiscard]] static std::expected<std::string, ParamError> getQueryParam(httpd_req_t* req, const char* key)
{
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) {
    return std::unexpected(ParamError::Missing);
  }

  std::string query;
  query.resize(qlen + 1);
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
    return std::unexpected(ParamError::Missing);
  }

  char val[256];
  if (httpd_query_key_value(query.c_str(), key, val, sizeof(val)) != ESP_OK) {
    return std::unexpected(ParamError::Missing);
  }

  return urlDecode(val);
}

// The web UI sends booleans as "1"/"0"; "true"/"false" are accepted too.
[[nodiscard]] static std::optional<bool> parseBool(std::string_view str)
{
  if (str == "1") {
    return true;
  }
  if (str == "0") {
    return false;
  }
  return Convert::ToBool(str);
}

[[nodiscard]] static std::expected<bool, ParamError> getBoolQueryParam(httpd_req_t* req, const char* key)
{
  auto str = getQueryParam(req, key);
  if (!str) {
    return std::unexpected(str.error());
  }
  auto value = parseBool(*str);
  if (!value) {
    return std::unexpected(ParamError::Invalid);
  }
  return *value;
}

static esp_err_t sendParamError(httpd_req_t* req, ParamError error)
{
  return sendResp(req, S400, HTTP::ContentType::JSON, error == ParamError::Missing ? JSON_ERR_MISSING_PARAM : JSON_ERR_INVALID_PARAM);
}

// Read a urlencoded request body (application/x-www-form-urlencoded).
[[nodiscard]] static std::optional<std::string> recvBody(httpd_req_t* req)
{
  int total = req->content_len;
  if (total <= 0 || total > 4096) {
    return std::nullopt;
  }

  // A client that announces a body and then stalls must not hold the (single) httpd task forever.
  static constexpr int kMaxRecvTimeouts = 3;

  std::string out;
  out.resize(total);
  int off      = 0;
  int timeouts = 0;
  while (off < total) {
    int r = httpd_req_recv(req, out.data() + off, total - off);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) {
      if (++timeouts >= kMaxRecvTimeouts) {
        return std::nullopt;
      }
      continue;
    }
    if (r <= 0) {
      return std::nullopt;
    }
    off += r;
  }
  return out;
}

[[nodiscard]] static std::expected<std::string, ParamError> getFormParam(const std::string& body, const char* key)
{
  char val[256];
  if (httpd_query_key_value(body.c_str(), key, val, sizeof(val)) != ESP_OK) {
    return std::unexpected(ParamError::Missing);
  }
  return urlDecode(val);
}

static const char* contentTypeForPath(const std::string& path)
{
  auto endsWith = [&](const char* ext) {
    size_t n = strlen(ext);
    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
  };

  if (endsWith(".html")) return HTTP::ContentType::TextHTML;
  if (endsWith(".js")) return HTTP::ContentType::JavaScript;
  if (endsWith(".css")) return HTTP::ContentType::CSS;
  if (endsWith(".svg")) return HTTP::ContentType::SVG;
  if (endsWith(".png")) return HTTP::ContentType::PNG;
  if (endsWith(".ico")) return HTTP::ContentType::Icon;
  if (endsWith(".json")) return HTTP::ContentType::JSON;
  if (endsWith(".woff2")) return HTTP::ContentType::WOFF2;
  if (endsWith(".txt")) return HTTP::ContentType::TextPlain;
  return HTTP::ContentType::OctetStream;
}

// ---------------------------------------------------------------------------
// API handlers (ported 1:1 from the ESPAsyncWebServer lambdas)
// ---------------------------------------------------------------------------

static esp_err_t apiBoard(httpd_req_t* req)
{
  bool hasPredefinedPins = Board::Get().rfTxGpio != OPENSHOCK_GPIO_INVALID;
  return sendResp(req, S200, HTTP::ContentType::JSON, hasPredefinedPins ? "{\"has_predefined_pins\":true}" : "{\"has_predefined_pins\":false}");
}

static esp_err_t apiPortalClose(httpd_req_t* req)
{
  CaptivePortal::SetUserDone();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiScan(httpd_req_t* req)
{
  auto run = getBoolQueryParam(req, "run");
  if (!run && run.error() == ParamError::Invalid) {
    return sendParamError(req, run.error());
  }
  if (run.value_or(true)) {
    WiFiScanManager::StartScan();
  } else {
    WiFiScanManager::AbortScan();
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiNetworksDelete(httpd_req_t* req)
{
  auto ssid = getQueryParam(req, "ssid");
  if (!ssid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  if (!WiFiManager::Forget(ssid->c_str())) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t sendAccountLinkResult(httpd_req_t* req, OpenShock::AccountLinkResultCode result)
{
  if (result == OpenShock::AccountLinkResultCode::Success) {
    return sendResp(req, S200, nullptr, {});
  }
  const char* error = OpenShock::AccountLinkResultCodeToString(result);
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  OpenShock::JSON::objSetString(gen, "error", error);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S400, HTTP::ContentType::JSON, json);
}

namespace {
  struct AccountLinkJob {
    // Keeps the instance alive, so its server can't be stopped under the async request; the manager's RetiredList
    // destroys it only after this is dropped.
    std::shared_ptr<CaptivePortal::CaptivePortalInstance> instance;
    httpd_req_t* req;  // async copy, owned until httpd_req_async_handler_complete
    std::string code;
  };

  std::atomic<bool> s_accountLinkInProgress = false;

  // Linking does DNS + TLS + an HTTPS round trip (seconds); running it here keeps the single httpd task free to
  // serve the page, assets and WebSocket meanwhile.
  void accountLinkTask(void* arg)
  {
    auto* job   = static_cast<AccountLinkJob*>(arg);
    auto result = GatewayConnectionManager::Link(std::string_view(job->code));

    sendAccountLinkResult(job->req, result);
    httpd_req_async_handler_complete(job->req);

    delete job;
    s_accountLinkInProgress.store(false);
    vTaskDelete(nullptr);
  }
}  // namespace

static esp_err_t apiAccountLink(httpd_req_t* req)
{
  if (!getAccountLinkRateLimiter().tryRequest()) {
    return sendResp(req, S429, HTTP::ContentType::JSON, JSON_ERR_RATE_LIMITED);
  }
  auto code = getQueryParam(req, "code");
  if (!code) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_CODE_REQUIRED);
  }

  // Null while the instance is still being constructed or once it is retired
  auto instance = CaptivePortal::GetLiveInstance(static_cast<CaptivePortal::CaptivePortalInstance*>(req->user_ctx));
  if (instance == nullptr) {
    return sendResp(req, S503, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }

  if (s_accountLinkInProgress.exchange(true)) {
    return sendResp(req, S429, HTTP::ContentType::JSON, JSON_ERR_RATE_LIMITED);
  }

  httpd_req_t* asyncReq = nullptr;
  if (httpd_req_async_handler_begin(req, &asyncReq) != ESP_OK) {
    s_accountLinkInProgress.store(false);
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }

  auto* job = new AccountLinkJob {std::move(instance), asyncReq, std::move(*code)};
  // TLS needs a large stack
  if (TaskUtils::TaskCreateExpensive(accountLinkTask, "AccountLink", 8192, job, 1, nullptr) != pdPASS) {
    OS_LOGE(TAG, "Failed to create account link task");
    delete job;
    esp_err_t err = sendResp(asyncReq, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
    httpd_req_async_handler_complete(asyncReq);
    s_accountLinkInProgress.store(false);
    return err;
  }

  return ESP_OK;
}

static esp_err_t apiAccountDelete(httpd_req_t* req)
{
  GatewayConnectionManager::UnLink();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiConfigRfPin(httpd_req_t* req)
{
  auto pinStr = getQueryParam(req, "pin");
  if (!pinStr) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  auto parsedPin = Convert::ToGpioNum(*pinStr);
  if (!parsedPin) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  gpio_num_t pin   = *parsedPin;
  auto result      = CommandHandler::SetRfTxPin(pin);
  using ResultCode = OpenShock::SetGPIOResultCode;
  if (result != ResultCode::Success) {
    const char* error = result == ResultCode::InvalidPin ? JSON_ERR_INVALID_PIN : result == ResultCode::PinInUse ? JSON_ERR_PIN_IN_USE : JSON_ERR_INTERNAL;
    return sendResp(req, S400, HTTP::ContentType::JSON, error);
  }
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  json_gen_obj_set_int(gen, "pin", pin);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S200, HTTP::ContentType::JSON, json);
}

static esp_err_t apiWifiNetworksAdd(httpd_req_t* req)
{
  auto body = recvBody(req);
  if (!body) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }

  auto ssid = getFormParam(*body, "ssid");
  if (!ssid && ssid.error() == ParamError::Invalid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_SSID);
  }
  if (!ssid || ssid->empty() || ssid->length() > 32) {
    return sendResp(req, S400, HTTP::ContentType::JSON, !ssid || ssid->empty() ? JSON_ERR_MISSING_SSID : JSON_ERR_INVALID_SSID);
  }

  // Missing means an open network; an undecodable one is rejected rather than saved as a different password.
  auto passwordParam = getFormParam(*body, "password");
  if (!passwordParam && passwordParam.error() == ParamError::Invalid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
  }
  std::string password = std::move(passwordParam).value_or(std::string());
  if (!password.empty()) {
    if (password.length() < 8) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_SHORT);
    }
    if (password.length() > 63) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_LONG);
    }
  }

  bool connect = true;
  if (auto connectStr = getFormParam(*body, "connect")) {
    auto parsed = parseBool(*connectStr);
    if (!parsed) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
    connect = *parsed;
  }

  wifi_auth_mode_t authMode = WIFI_AUTH_MAX;
  if (auto securityStr = getFormParam(*body, "security")) {
    auto sec = Convert::ToUint8(*securityStr);
    if (!sec || *sec > static_cast<uint8_t>(WIFI_AUTH_MAX)) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
    authMode = static_cast<wifi_auth_mode_t>(*sec);
  }

  if (!WiFiManager::Save(ssid->c_str(), std::string_view(password), connect, authMode)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiConnect(httpd_req_t* req)
{
  auto body = recvBody(req);
  if (!body) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  auto ssid = getFormParam(*body, "ssid");
  if (!ssid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  if (!WiFiManager::Connect(ssid->c_str())) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiDisconnect(httpd_req_t* req)
{
  WiFiManager::Disconnect();
  return sendResp(req, S200, nullptr, {});
}

// PUT /api/ota/settings?enabled=&channel=&interval=&allow=&require=
// Any subset of the OTA settings in one request, applied together in a single config write (and none at all when
// nothing changed), instead of one handler and one flash write per setting.
static esp_err_t apiOtaSettings(httpd_req_t* req)
{
  // Parse everything first, then apply it in one atomic update so a concurrent writer (backend, serial) isn't undone
  std::optional<bool> enabled, allow, require;
  std::optional<OtaUpdateChannel> channel;
  std::optional<uint16_t> interval;

  // Reads one optional boolean parameter; returns false (after responding) on an invalid value.
  auto parseBool = [&](const char* key, std::optional<bool>& out, esp_err_t& response) {
    auto value = getBoolQueryParam(req, key);
    if (!value) {
      if (value.error() == ParamError::Missing) return true;
      response = sendParamError(req, value.error());
      return false;
    }
    out = *value;
    return true;
  };

  esp_err_t response = ESP_OK;
  if (!parseBool("enabled", enabled, response) || !parseBool("allow", allow, response) || !parseBool("require", require, response)) {
    return response;
  }

  if (auto channelStr = getQueryParam(req, "channel")) {
    channel = TryParseOtaUpdateChannel(*channelStr);
    if (!channel) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_CHANNEL);
    }
  }

  if (auto intervalStr = getQueryParam(req, "interval")) {
    // Minutes; 0 would make periodic checks fire on every OTA task wake-up
    interval = Convert::ToUint16(*intervalStr);
    if (!interval || *interval == 0) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
  }

  if (!enabled && !allow && !require && !channel && !interval) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }

  bool saved = Config::UpdateOtaUpdateConfig([&](Config::OtaUpdateConfig& cfg) {
    bool changed = false;
    auto apply   = [&changed](auto& field, const auto& value) {
      if (value && field != *value) {
        field   = *value;
        changed = true;
      }
    };
    apply(cfg.isEnabled, enabled);
    apply(cfg.allowBackendManagement, allow);
    apply(cfg.requireManualApproval, require);
    apply(cfg.updateChannel, channel);
    apply(cfg.checkInterval, interval);
    return changed;
  });
  if (!saved) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaCheck(httpd_req_t* req)
{
  // Checks the configured channel; the update task reports progress over the gateway as usual.
  if (!AppHooks::OtaRequestUpdateCheck()) {
    return sendResp(req, S503, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

// ---------------------------------------------------------------------------
// Static file serving (gzipped assets from the raw littlefs static0 partition)
// ---------------------------------------------------------------------------

esp_err_t CaptivePortal::CaptivePortalInstance::staticFileHandler(httpd_req_t* req)
{
  auto* self = static_cast<CaptivePortalInstance*>(req->user_ctx);

  if (!self->m_staticFs.isMounted()) {
    // Filesystem image was never uploaded; serve the help page for any request.
    return sendResp(
      req,
      S200,
      HTTP::ContentType::TextPlain,
      R"(You probably forgot to upload the Filesystem with PlatformIO!
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
    // Unknown path -> captive redirect (matches serveStatic default-file fallthrough).
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
    // Headers/chunks may already be on the wire; can't cleanly redirect now.
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

#if defined(CONFIG_HTTPD_QUEUE_WORK_BLOCKING)
  // httpd_queue_work is called from the httpd task itself (drainSends); a blocking mailbox would deadlock it
#error "CONFIG_HTTPD_QUEUE_WORK_BLOCKING must be disabled"
#endif

  // Bounds the memory a burst of broadcasts can hold while no client drains them
  constexpr size_t kMaxQueuedSends = 32;

  void sendToWsClient(httpd_handle_t hd, int fd, httpd_ws_frame_t& frame)
  {
    if (httpd_ws_send_frame_async(hd, fd, &frame) != ESP_OK) {
      // A client that can't take data (gone, asleep, buffers full) would otherwise stall every later send for the
      // full send timeout; drop it, the web UI reconnects.
      httpd_sess_trigger_close(hd, fd);
      return;
    }

    // httpd only counts inbound requests as activity, so a push-only WebSocket would look idle to the LRU purge.
    httpd_sess_update_lru_counter(hd, fd);
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
      m_clients[i].used        = true;
      m_clients[i].fd          = fd;
      m_clients[i].reasmActive = false;
      m_clients[i].reasmType   = WebSocketMessageType::Binary;
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
        m_clients[i].used        = false;
        m_clients[i].reasmActive = false;
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

// httpd does not call the URI handler for the handshake (esp_http_server's httpd_uri.c), only this callback, right
// after it has switched the socket to WebSocket: allocate an id, arm the disconnect hook and greet the client.
esp_err_t CaptivePortal::CaptivePortalInstance::wsPostHandshake(httpd_req_t* req)
{
  auto* self = static_cast<CaptivePortalInstance*>(req->user_ctx);
  int fd     = httpd_req_to_sockfd(req);

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

esp_err_t CaptivePortal::CaptivePortalInstance::wsHandler(httpd_req_t* req)
{
  auto* self = static_cast<CaptivePortalInstance*>(req->user_ctx);
  int fd     = httpd_req_to_sockfd(req);

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
    // Defensive: wsPostHandshake() registers every client, so a frame from an unknown fd should not happen.
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
      {
        ScopedLock lock__(&m_clientsMutex);
        if (m_clients[idx].reasmActive) {
          // A new message started before the previous fragmented one finished; drop the stale fragments
          OS_LOGW(TAG, "WebSocket client %u started a new message mid-fragment, dropping the old one", socketId);
          m_clients[idx].reasmActive = false;
          m_clients[idx].reasm.clear();
        }
        if (!final) {
          m_clients[idx].reasmActive = true;
          m_clients[idx].reasmType   = type;
          m_clients[idx].reasm.assign(payload.begin(), payload.end());
          break;
        }
      }
      dispatchWsMessage(socketId, type, payload);
      break;
    }
    case HTTPD_WS_TYPE_CONTINUE:
    {
      WebSocketMessageType type;
      std::vector<uint8_t> full;
      {
        ScopedLock lock__(&m_clientsMutex);
        if (!m_clients[idx].reasmActive) {
          OS_LOGW(TAG, "WebSocket client %u sent a continuation frame without a message start, dropping it", socketId);
          break;
        }
        // MAX_WS_MSG only bounds single frames; also bound the reassembled message
        if (m_clients[idx].reasm.size() + payload.size() > MAX_WS_MSG) {
          OS_LOGE(TAG, "WebSocket client %u fragmented message exceeds %u bytes, closing", socketId, static_cast<unsigned>(MAX_WS_MSG));
          m_clients[idx].reasmActive = false;
          m_clients[idx].reasm.clear();
          m_clients[idx].reasm.shrink_to_fit();
          httpd_sess_trigger_close(m_server, fd);
          break;
        }
        m_clients[idx].reasm.insert(m_clients[idx].reasm.end(), payload.begin(), payload.end());
        if (!final) {
          break;
        }
        type                       = m_clients[idx].reasmType;
        full                       = std::move(m_clients[idx].reasm);
        m_clients[idx].reasmActive = false;
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

  // We're on the httpd task with a live req; send directly, no copy/queue needed.
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

  // Nothing scanned yet (e.g. connected at boot, so no auto-scan ran): start one so the list isn't empty
  if (networks.empty() && !WiFiScanManager::IsScanning()) {
    WiFiScanManager::StartScan();
  }
}

void CaptivePortal::CaptivePortalInstance::handleWebSocketClientDisconnected(uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

// ---------------------------------------------------------------------------
// Public WS send API (called from arbitrary tasks -> must copy + queue)
// ---------------------------------------------------------------------------

bool CaptivePortal::CaptivePortalInstance::queueSend(int fd, bool broadcast, bool binary, std::span<const uint8_t> data)
{
  if (m_server == nullptr) {
    return false;
  }

  {
    ScopedLock lock__(&m_sendMutex);
    if (m_sendQueue.size() >= kMaxQueuedSends) {
      OS_LOGW(TAG, "WebSocket send queue full, dropping message");
      return false;
    }
    m_sendQueue.push_back(WsSend {.fd = broadcast ? -1 : fd, .binary = binary, .data = std::vector<uint8_t>(data.begin(), data.end())});
  }

  // Ring the doorbell unless a drain is already pending; that drain will pick this message up too
  if (!m_drainQueued.exchange(true)) {
    if (httpd_queue_work(m_server, &CaptivePortalInstance::wsDrainWork, this) != ESP_OK) {
      m_drainQueued.store(false);
      // The message stays queued and goes out with the next send that gets a drain scheduled
      OS_LOGW(TAG, "httpd work queue full, WebSocket send delayed");
    }
  }
  return true;
}

// Runs on the httpd task. httpd_stop (in the destructor) joins that task before any member is destroyed, and a drain
// still in httpd's mailbox then never runs, so `this` is valid here.
void CaptivePortal::CaptivePortalInstance::wsDrainWork(void* arg)
{
  static_cast<CaptivePortalInstance*>(arg)->drainSends();
}

void CaptivePortal::CaptivePortalInstance::drainSends()
{
  while (true) {
    std::deque<WsSend> batch;
    {
      ScopedLock lock__(&m_sendMutex);
      batch.swap(m_sendQueue);
    }

    for (auto& send : batch) {
      httpd_ws_frame_t frame = {};
      frame.final            = true;
      frame.type             = send.binary ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT;
      frame.payload          = send.data.data();
      frame.len              = send.data.size();

      if (send.fd >= 0) {
        sendToWsClient(m_server, send.fd, frame);
        continue;
      }

      size_t count = 8;
      int fds[8];
      if (httpd_get_client_list(m_server, &count, fds) == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
          if (httpd_ws_get_fd_info(m_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            sendToWsClient(m_server, fds[i], frame);
          }
        }
      }
    }

    // Clear the doorbell, then look again: a producer that saw it set before the clear didn't schedule a drain
    m_drainQueued.store(false);
    {
      ScopedLock lock__(&m_sendMutex);
      if (m_sendQueue.empty()) {
        return;
      }
    }
    if (m_drainQueued.exchange(true)) {
      return;  // A producer scheduled a new drain meanwhile
    }
  }
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
  // No /api/config/estop/*: the portal is an open, unauthenticated AP, so the E-Stop is configured over serial only.
  // No /api/ota/domain: the portal is an open, unauthenticated AP, and OTA trusts the hashes served by that domain, so
  // changing it would let anyone in range install firmware. It can only be changed over serial (jsonconfig).
  reg("/api/ota/settings", HTTP_PUT, apiOtaSettings);
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
  ws.ws_post_handshake_cb     = &CaptivePortalInstance::wsPostHandshake;
  if (httpd_register_uri_handler(m_server, &ws) != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WebSocket handler");
  }

  // Static files: catch-all, registered LAST so specific routes win.
  reg("/*", HTTP_GET, &CaptivePortalInstance::staticFileHandler);
}

bool CaptivePortal::CaptivePortalInstance::startHttpServer()
{
  httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
  config.server_port      = HTTP_PORT;
  config.max_uri_handlers = 40;
  // Browsers open several connections per page load; with only 4 slots the LRU purge evicted the (server-push,
  // therefore "idle" looking) WebSocket. Needs CONFIG_LWIP_MAX_SOCKETS >= max_open_sockets + 3 (+ DNS/HTTP client).
  config.max_open_sockets = 8;
  config.lru_purge_enable = true;
  config.stack_size       = 8192;
  // Shorter than the default 10 s: one stalled or sleeping client blocks the single httpd task for this long.
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  config.uri_match_fn      = httpd_uri_match_wildcard;
  // Same core as the WiFi/lwIP tasks, away from the E-Stop and RF tasks on core 1
  config.core_id = 0;

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
  const esp_partition_t* partition = FindStaticPartition();
  if (partition == nullptr) {
    OS_LOGE(TAG, "Failed to find static filesystem partition");
  } else if (!m_staticFs.mount(partition)) {
    OS_LOGE(TAG, "Failed to mount static filesystem");
  } else if (!m_staticFs.exists("/www/index.html.gz")) {
    OS_LOGE(TAG, "/www/index.html.gz not found, serving error page");
  } else {
    m_fsHash = getPartitionHash();
    OS_LOGI(TAG, "Serving files from littlefs (hash: %s)", m_fsHash != nullptr ? m_fsHash : "?");
  }

  // Start the combined HTTP + WebSocket server on port 80.
  if (!startHttpServer()) {
    return;
  }

  // Start the wildcard DNS responder (all A queries -> the portal AP IP).
  m_dnsStarted = m_dnsServer.start(CaptivePortal::ApIPv4String());
  if (!m_dnsStarted) {
    OS_LOGE(TAG, "Failed to start DNS server");
  }
}

CaptivePortal::CaptivePortalInstance::~CaptivePortalInstance()
{
  m_dnsServer.stop();

  // No account link can still be answering on this server: its job holds a reference, so this only runs after it.

  // Stop the server (closes all WS sockets, firing the session free callbacks).
  if (m_server != nullptr) {
    httpd_stop(m_server);
    m_server = nullptr;
  }

  m_staticFs.unmount();
}
