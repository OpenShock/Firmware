#include <freertos/FreeRTOS.h>

#include "http/HTTPClientState.h"

const char* const TAG = "HTTPClientState";

#include "Common.h"
#include "Convert.h"
#include "Logging.h"

#include <algorithm>

static const uint32_t HTTP_BUFFER_SIZE = 4096LLU;
static const uint32_t HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

using namespace OpenShock;

HTTP::HTTPClientState::HTTPClientState(uint32_t timeoutMs)
  : m_handle(nullptr)
  , m_reading(false)
  , m_retryAfterSeconds(0)
  , m_headers()
{
  esp_http_client_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.user_agent            = OpenShock::Constants::FW_USERAGENT;
  cfg.timeout_ms            = static_cast<int>(std::min<uint32_t>(timeoutMs, INT32_MAX));
  cfg.disable_auto_redirect = true;
  cfg.event_handler         = HTTPClientState::EventHandler;
  cfg.transport_type        = HTTP_TRANSPORT_OVER_SSL;
  cfg.user_data             = reinterpret_cast<void*>(this);
  cfg.is_async              = false;
  cfg.use_global_ca_store   = true;
  #warning This still uses SSL, upgrade to TLS! (latest ESP-IDF)

  m_handle = esp_http_client_init(&cfg);
}

HTTP::HTTPClientState::~HTTPClientState()
{
  if (m_handle != nullptr) {
    esp_http_client_cleanup(m_handle);
    m_handle = nullptr;
  }
}

HTTP::HTTPClientState::StartRequestResult HTTP::HTTPClientState::StartRequest(esp_http_client_method_t method, const char* url, int writeLen)
{
  esp_err_t err;

  if (m_reading) {
    return { .error = HTTPError::ClientBusy };
  }

  m_retryAfterSeconds = 0;
  m_headers.clear();

  err = esp_http_client_set_url(m_handle, url);
  if (err != ESP_OK) return { .error = HTTPError::InvalidUrl };

  err = esp_http_client_set_method(m_handle, method);
  if (err != ESP_OK) return { .error = HTTPError::InvalidHttpMethod };

  err = esp_http_client_open(m_handle, writeLen);
  if (err != ESP_OK) return { .error = HTTPError::NetworkError };

  int contentLength = esp_http_client_fetch_headers(m_handle);
  if (contentLength < 0) return { .error = HTTPError::NetworkError };

  if (m_retryAfterSeconds > 0) {
    uint32_t retryAfterSeconds = m_retryAfterSeconds;
    m_retryAfterSeconds = 0;
    return { .error = HTTPError::RateLimited, .retryAfterSeconds = retryAfterSeconds };
  }

  bool isChunked = false;
  if (contentLength == 0) {
    isChunked = esp_http_client_is_chunked_response(m_handle);
  }

  int statusCode = esp_http_client_get_status_code(m_handle);
  if (statusCode < 0 || statusCode > 599) {
    OS_LOGE(TAG, "Returned statusCode is invalid (%i)", statusCode);
    return { .error = HTTPError::NetworkError };
  }

  m_reading = true;

  return StartRequestResult {
    .statusCode = static_cast<uint16_t>(statusCode),
    .isChunked = isChunked,
    .contentLength = static_cast<uint32_t>(contentLength),
    .headers = std::move(m_headers)
  };
}

HTTP::ReadResult<uint32_t> HTTP::HTTPClientState::ReadStreamImpl(DownloadCallback cb)
{
  if (m_handle == nullptr || !m_reading) {
    m_reading = false;
    return HTTPError::ConnectionClosed;
  }

  uint32_t totalWritten = 0;
  uint8_t  buffer[HTTP_BUFFER_SIZE];

  while (true) {
    if (totalWritten >= HTTP_DOWNLOAD_SIZE_LIMIT) {
      m_reading = false;
      return HTTPError::SizeLimitExceeded;
    }

    uint32_t remaining = HTTP_DOWNLOAD_SIZE_LIMIT - totalWritten;
    int toRead = static_cast<int>(std::min<uint32_t>(HTTP_BUFFER_SIZE, remaining));

    int n = esp_http_client_read(
      m_handle,
      reinterpret_cast<char*>(buffer),
      toRead
    );

    if (n < 0) {
      m_reading = false;
      return HTTPError::NetworkError;
    }

    if (n == 0) {
      // EOF
      break;
    }

    uint32_t chunkLen = static_cast<uint32_t>(n);
    if (!cb(totalWritten, buffer, chunkLen)) {
      m_reading = false;
      return HTTPError::Aborted;
    }

    totalWritten += chunkLen;
  }

  m_reading = false;
  return totalWritten;
}

HTTP::ReadResult<std::string> HTTP::HTTPClientState::ReadStringImpl(uint32_t reserve)
{
  std::string result;
  if (reserve > 0) {
    result.reserve(reserve);
  }

  auto writer = [&result](std::size_t offset, const uint8_t* data, std::size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  };

  auto response = ReadStreamImpl(writer);
  if (response.error != HTTPError::None) {
    return response.error;
  }

  return result;
}

esp_err_t HTTP::HTTPClientState::EventHandler(esp_http_client_event_t* evt)
{
  HTTPClientState* client = reinterpret_cast<HTTPClientState*>(evt->user_data);

  switch (evt->event_id)
  {
  case HTTP_EVENT_ERROR:
    OS_LOGE(TAG, "Got error event");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    OS_LOGI(TAG, "Got connected event");
    break;
  case HTTP_EVENT_HEADERS_SENT:
    OS_LOGI(TAG, "Got headers_sent event");
    break;
  case HTTP_EVENT_ON_HEADER:
    return client->EventHeaderHandler(evt->header_key, evt->header_value);
  case HTTP_EVENT_ON_DATA:
    OS_LOGI(TAG, "Got on_data event");
    break;
  case HTTP_EVENT_ON_FINISH:
    OS_LOGI(TAG, "Got on_finish event");
    break;
  case HTTP_EVENT_DISCONNECTED:
    OS_LOGI(TAG, "Got disconnected event");
    break;
  default:
    OS_LOGE(TAG, "Got unknown event");
    break;
  }

  return ESP_OK;
}

esp_err_t HTTP::HTTPClientState::EventHeaderHandler(std::string key, std::string value)
{
  OS_LOGI(TAG, "Got header_received event: %.*s - %.*s", key.length(), key.c_str(), key.length(), key.c_str());

  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });

  if (key == "retry-after") {
    uint32_t seconds = 0;
    if (!Convert::ToUint32(value, seconds) || seconds <= 0) {
      seconds = 15;
    }

    OS_LOGI(TAG, "Retry-After: %d seconds, applying delay to rate limiter", seconds);
    m_retryAfterSeconds = seconds;
  }

  m_headers[key] = std::move(value);

  return ESP_OK;
}
