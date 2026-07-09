#include <freertos/FreeRTOS.h>

#include "http/HTTPRequestManager.h"

const char* const TAG = "HTTPRequestManager";

#include "Logging.h"
#include "OpenShock.h"
#include "RateLimiter.h"
#include "SimpleMutex.h"
#include "Temporal.h"
#include "util/StringUtils.h"

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

using namespace std::string_view_literals;

const std::size_t HTTP_BUFFER_SIZE = 4096LLU;
const int HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

static OpenShock::SimpleMutex s_rateLimitsMutex                                              = {};
static std::unordered_map<std::string, std::shared_ptr<OpenShock::RateLimiter>> s_rateLimits = {};

using namespace OpenShock;

static std::string_view getDomainFromURL(std::string_view url)
{
  if (url.empty()) {
    return {};
  }

  // Remove the protocol eg. "https://api.example.com:443/path" -> "api.example.com:443/path"
  auto seperator = url.find("://");
  if (seperator != std::string_view::npos) {
    url = url.substr(seperator + 3);
  }

  // Remove the path eg. "api.example.com:443/path" -> "api.example.com:443"
  seperator = url.find('/');
  if (seperator != std::string_view::npos) {
    url = url.substr(0, seperator);
  }

  // Remove the port eg. "api.example.com:443" -> "api.example.com"
  seperator = url.rfind(':');
  if (seperator != std::string_view::npos) {
    url = url.substr(0, seperator);
  }

  // Remove all subdomains eg. "api.example.com" -> "example.com"
  seperator = url.rfind('.');
  if (seperator == std::string_view::npos) {
    return url;  // E.g. "localhost"
  }
  seperator = url.rfind('.', seperator - 1);
  if (seperator != std::string_view::npos) {
    url = url.substr(seperator + 1);
  }

  return url;
}

static std::shared_ptr<OpenShock::RateLimiter> createRateLimiterForDomain(std::string_view domain)
{
  auto rateLimit = std::make_shared<OpenShock::RateLimiter>();

  // Add default limits
  rateLimit->addLimit(1000, 5);        // 5 per second
  rateLimit->addLimit(10 * 1000, 10);  // 10 per 10 seconds

  // per-domain limits
  if (domain == CONFIG_OPENSHOCK_API_DOMAIN) {
    rateLimit->addLimit(60 * 1000, 12);        // 12 per minute
    rateLimit->addLimit(60 * 60 * 1000, 120);  // 120 per hour
  }

  return rateLimit;
}

static std::shared_ptr<OpenShock::RateLimiter> createRateLimiterForURL(std::string_view url)
{
  auto domain = std::string(getDomainFromURL(url));
  if (domain.empty()) {
    return nullptr;
  }

  OpenShock::ScopedLock lock__(&s_rateLimitsMutex);

  auto it = s_rateLimits.find(domain);
  if (it == s_rateLimits.end()) {
    it = s_rateLimits.emplace(domain, createRateLimiterForDomain(domain)).first;
  }

  return it->second;
}

HTTP::Client::Client() noexcept
  : m_handle(nullptr)
  , m_headerKeys()
  , m_retryAfter()
  , m_connectionClose(false)
{
}

HTTP::Client::~Client()
{
  drop();
}

// esp_http_client only exposes response headers through the event stream, so we
// capture the ones we care about here. user_data points at the owning Client.
esp_err_t HTTP::Client::eventHandler(esp_http_client_event_t* evt)
{
  if (evt->event_id != HTTP_EVENT_ON_HEADER) {
    return ESP_OK;
  }

  auto* self = static_cast<Client*>(evt->user_data);
  if (self == nullptr) {
    return ESP_OK;
  }

  if (OpenShock::StringIEquals(evt->header_key, "Retry-After")) {
    self->m_retryAfter.assign(evt->header_value);
  } else if (OpenShock::StringIEquals(evt->header_key, "Connection")) {
    self->m_connectionClose = OpenShock::StringIEquals(evt->header_value, "close");
  }

  return ESP_OK;
}

void HTTP::Client::drop()
{
  if (m_handle != nullptr) {
    esp_http_client_close(m_handle);
    esp_http_client_cleanup(m_handle);
    m_handle = nullptr;
  }
  m_headerKeys.clear();
}

HTTP::Response<std::size_t>
  HTTP::Client::Download(std::string_view url, const std::map<std::string, std::string>& headers, HTTP::GotContentLengthCallback contentLengthCallback, HTTP::DownloadCallback downloadCallback, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs)
{
  std::shared_ptr<OpenShock::RateLimiter> rateLimiter = createRateLimiterForURL(url);
  if (rateLimiter == nullptr) {
    return {RequestResult::InvalidURL, 0, 0};
  }

  if (!rateLimiter->tryRequest()) {
    return {RequestResult::RateLimited, 0, 0};
  }

  std::string urlStr(url);
  m_retryAfter.clear();
  m_connectionClose = false;

  int64_t begin = OpenShock::millis();

  // Open the connection, reusing the kept-alive handle when possible. If a
  // reused (kept-alive) socket has been closed by the server, drop it and
  // reconnect fresh once. HTTPS servers are verified against the compiled-in
  // CA bundle via config.crt_bundle_attach below.
  esp_err_t err = ESP_FAIL;
  for (int attempt = 0; attempt < 2; ++attempt) {
    bool reused = m_handle != nullptr;

    if (m_handle == nullptr) {
      esp_http_client_config_t config = {};
      config.url                      = urlStr.c_str();
      config.user_agent               = OpenShock::Constants::FW_USERAGENT;
      config.method                   = HTTP_METHOD_GET;
      config.timeout_ms               = static_cast<int>(timeoutMs);
      config.keep_alive_enable        = true;
      config.event_handler            = &Client::eventHandler;
      config.user_data                = this;
      config.crt_bundle_attach        = esp_crt_bundle_attach;  // verify HTTPS servers against the compiled-in CA bundle

      m_handle = esp_http_client_init(&config);
      if (m_handle == nullptr) {
        OS_LOGE(TAG, "Failed to initialize HTTP client");
        return {RequestResult::RequestFailed, 0, 0};
      }
    } else {
      esp_http_client_set_url(m_handle, urlStr.c_str());
      esp_http_client_set_method(m_handle, HTTP_METHOD_GET);
      esp_http_client_set_timeout_ms(m_handle, static_cast<int>(timeoutMs));
    }

    // Clear headers left over from a previous request on this reused handle.
    for (const auto& key : m_headerKeys) {
      esp_http_client_delete_header(m_handle, key.c_str());
    }
    m_headerKeys.clear();

    for (const auto& header : headers) {
      esp_http_client_set_header(m_handle, header.first.c_str(), header.second.c_str());
      m_headerKeys.push_back(header.first);
    }

    err = esp_http_client_open(m_handle, 0);
    if (err == ESP_OK) {
      break;
    }

    drop();
    if (!reused) {
      OS_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
      return {RequestResult::RequestFailed, 0, 0};
    }
    OS_LOGD(TAG, "Reused connection failed to open, reconnecting");
  }
  if (err != ESP_OK) {
    return {RequestResult::RequestFailed, 0, 0};
  }

  // Tears down the connection when it can't be safely reused.
  auto fail = [&](HTTP::RequestResult result, int code, std::size_t written) -> HTTP::Response<std::size_t> {
    drop();
    return {result, code, written};
  };

  int64_t contentLength = esp_http_client_fetch_headers(m_handle);
  if (contentLength < 0) {
    OS_LOGE(TAG, "Failed to fetch response headers");
    return fail(RequestResult::RequestFailed, 0, 0);
  }

  int responseCode = esp_http_client_get_status_code(m_handle);

  if (begin + timeoutMs < OpenShock::millis()) {
    OS_LOGW(TAG, "Request timed out");
    return fail(RequestResult::TimedOut, responseCode, 0);
  }

  if (responseCode == 429) {  // Too Many Requests
    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Retry-After
    long retryAfter = 0;
    if (!m_retryAfter.empty() && std::all_of(m_retryAfter.begin(), m_retryAfter.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
      retryAfter = strtol(m_retryAfter.c_str(), nullptr, 10);
    }

    // If header missing/unparseable, default to 15 seconds
    if (retryAfter <= 0) {
      retryAfter = 15;
    }

    rateLimiter->blockFor(retryAfter * 1000);

    return fail(RequestResult::RateLimited, responseCode, 0);  // body not drained
  }

  if (responseCode == 418) {
    OS_LOGW(TAG, "The server refused to brew coffee because it is, permanently, a teapot.");
  }

  if (std::find(acceptedCodes.begin(), acceptedCodes.end(), responseCode) == acceptedCodes.end()) {
    OS_LOGD(TAG, "Received unexpected response code %d", responseCode);
    return fail(RequestResult::CodeRejected, responseCode, 0);  // body not drained
  }

  bool chunked = esp_http_client_is_chunked_response(m_handle);

  if (contentLength > HTTP_DOWNLOAD_SIZE_LIMIT) {
    OS_LOGE(TAG, "Content-Length too large");
    return fail(RequestResult::RequestFailed, responseCode, 0);
  }

  if (contentLength > 0) {
    if (!contentLengthCallback(static_cast<int>(contentLength))) {
      OS_LOGW(TAG, "Request cancelled by callback");
      return fail(RequestResult::Cancelled, responseCode, 0);
    }
  } else if (!chunked) {
    // No Content-Length and not chunked => empty body. Nothing to drain.
    if (m_connectionClose) {
      drop();
    }
    return {RequestResult::Success, responseCode, 0};
  }

  uint8_t* buffer = static_cast<uint8_t*>(malloc(HTTP_BUFFER_SIZE));
  if (buffer == nullptr) {
    OS_LOGE(TAG, "Failed to allocate HTTP buffer");
    return fail(RequestResult::InternalError, responseCode, 0);
  }

  std::size_t totalWritten   = 0;
  HTTP::RequestResult result = HTTP::RequestResult::Success;

  // esp_http_client_read transparently decodes chunked transfer-encoding.
  int read;
  while ((read = esp_http_client_read(m_handle, reinterpret_cast<char*>(buffer), static_cast<int>(HTTP_BUFFER_SIZE))) > 0) {
    if (begin + timeoutMs < OpenShock::millis()) {
      OS_LOGW(TAG, "Request timed out");
      result = HTTP::RequestResult::TimedOut;
      break;
    }

    if (!downloadCallback(totalWritten, buffer, static_cast<std::size_t>(read))) {
      OS_LOGW(TAG, "Request cancelled by callback");
      result = HTTP::RequestResult::Cancelled;
      break;
    }

    totalWritten += static_cast<std::size_t>(read);
  }

  free(buffer);

  if (result == HTTP::RequestResult::Success && (read < 0 || !esp_http_client_is_complete_data_received(m_handle))) {
    OS_LOGW(TAG, "Response body was not fully received");
    result = HTTP::RequestResult::RequestFailed;
  }

  if (result != HTTP::RequestResult::Success) {
    return fail(result, responseCode, totalWritten);  // connection state uncertain
  }

  // Success: keep the connection alive for the next request unless the server closed it.
  if (m_connectionClose) {
    drop();
  }

  return {result, responseCode, totalWritten};
}

HTTP::Response<std::string> HTTP::Client::GetString(std::string_view url, const std::map<std::string, std::string>& headers, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs)
{
  std::string result;

  auto allocator = [&result](std::size_t contentLength) {
    result.reserve(contentLength);
    return true;
  };
  auto writer = [&result](std::size_t offset, const uint8_t* data, std::size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  };

  auto response = Download(url, headers, allocator, writer, acceptedCodes, timeoutMs);
  if (response.result != RequestResult::Success) {
    return {response.result, response.code, {}};
  }

  return {response.result, response.code, result};
}
