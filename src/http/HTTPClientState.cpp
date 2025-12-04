#include "http/HTTPClientState.h"

const char* const TAG = "HTTPClientState";

#include "Common.h"
#include "Logging.h"

#include <algorithm>

static const std::size_t HTTP_BUFFER_SIZE = 4096LLU;
static const std::size_t HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

using namespace OpenShock;

HTTP::HTTPClientState::HTTPClientState(uint32_t timeoutMs)
  : m_handle(nullptr)
  , m_reading(false)
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

  err = esp_http_client_set_url(m_handle, url);
  if (err != ESP_OK) return {err, false, 0};

  err = esp_http_client_set_method(m_handle, method);
  if (err != ESP_OK) return {err, false, 0};

  err = esp_http_client_open(m_handle, writeLen);
  if (err != ESP_OK) return {err, false, 0};

  int responseLength = esp_http_client_fetch_headers(m_handle);
  if (responseLength == ESP_FAIL) return {err, false, 0};

  bool isChunked = false;
  if (responseLength == 0) {
    isChunked = esp_http_client_is_chunked_response(m_handle);
  }

  return {ESP_OK, isChunked, static_cast<uint32_t>(responseLength)};
}

HTTP::HTTPClientState::ReadResult HTTP::HTTPClientState::ReadStreamImpl(DownloadCallback cb)
{
  if (m_handle == nullptr || !m_reading) {
    m_reading = false;
    return {ReadResultCode::ConnectionClosed, 0};
  }

  std::size_t totalWritten = 0;
  uint8_t     buffer[HTTP_BUFFER_SIZE];

  while (true) {
    if (totalWritten >= HTTP_DOWNLOAD_SIZE_LIMIT) {
      m_reading = false;
      return {ReadResultCode::SizeLimitExceeded, totalWritten};
    }

    std::size_t remaining = HTTP_DOWNLOAD_SIZE_LIMIT - totalWritten;
    int toRead = static_cast<int>(std::min<std::size_t>(HTTP_BUFFER_SIZE, remaining));

    int n = esp_http_client_read(
      m_handle,
      reinterpret_cast<char*>(buffer),
      toRead
    );

    if (n < 0) {
      m_reading = false;
      return {ReadResultCode::NetworkError, totalWritten};
    }

    if (n == 0) {
      // EOF
      break;
    }

    std::size_t chunkLen = static_cast<std::size_t>(n);
    if (!cb(totalWritten, buffer, chunkLen)) {
      m_reading = false;
      return {ReadResultCode::Aborted, totalWritten};
    }

    totalWritten += chunkLen;
  }

  m_reading = false;
  return {ReadResultCode::Success, totalWritten};
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
    client->m_connected = true;
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
    client->m_connected = false;
    OS_LOGI(TAG, "Got disconnected event");
    break;
  default:
    OS_LOGE(TAG, "Got unknown event");
    break;
  }

  return ESP_OK;
}

esp_err_t HTTP::HTTPClientState::EventHeaderHandler(std::string_view key, std::string_view value)
{
    OS_LOGI(TAG, "Got header_received event: %s - %s", evt->header_key, evt->header_value);
}
