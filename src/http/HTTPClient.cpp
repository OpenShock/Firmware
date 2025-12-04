#include "http/HTTPClient.h"

const char* const TAG = "HTTPClient";

#include "http/RateLimiters.h"
#include "Logging.h"
#include "util/DomainUtils.h"

const std::size_t HTTP_BUFFER_SIZE = 4096LLU;
const int HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

using namespace OpenShock;

HTTP::HTTPClient::HTTPClient(uint32_t timeoutMs, const char* useragent)
{
  esp_http_client_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.timeout_ms            = static_cast<int>(std::min<uint32_t>(timeoutMs, INT32_MAX));
  cfg.disable_auto_redirect = true;
  cfg.event_handler         = HTTPClient::EventHandler;
  cfg.transport_type        = HTTP_TRANSPORT_OVER_SSL;
  cfg.user_data             = reinterpret_cast<void*>(this);
  cfg.is_async              = false;
  cfg.use_global_ca_store   = true;
  #warning This still uses SSL, upgrade to TLS! (latest ESP-IDF)

  m_handle = esp_http_client_init(&cfg);
}

HTTP::HTTPClient::~HTTPClient()
{
  esp_http_client_cleanup(m_handle);
}

esp_err_t HTTP::HTTPClient::SetHeaders(const std::map<std::string, std::string>& headers) {
  esp_err_t err;

  for (auto& header : headers) {
    err = SetHeader(header.first.c_str(), header.second.c_str());

    if (err != ESP_OK) return err;
  }

  return ESP_OK;
}

esp_err_t HTTP::HTTPClient::Get(const char* url) {
  esp_err_t err;

  err = Start(HTTP_METHOD_GET, url, 0);
  if (err != ESP_OK) return err;

  m_responseLength = esp_http_client_fetch_headers(m_handle);
  if (m_responseLength == ESP_FAIL) {
    return ESP_FAIL;
  }

  m_statusCode = esp_http_client_get_status_code(m_handle);

  return ESP_OK;
}

HTTP::Response<std::size_t> HTTP::HTTPClient::ReadResponseStream(DownloadCallback downloadCallback) {
  if (!m_connected) {
    return {DownloadResult::Closed, ESP_FAIL, 0};
  }

  std::size_t nWritten;

  return {DownloadResult::Success, ESP_OK, nWritten};
}

HTTP::Response<std::string> HTTP::HTTPClient::ReadResponseString() {
  std::string result;
  if (m_responseLength > 0) {
    result.reserve(m_responseLength);
  }

  auto writer = [&result](std::size_t offset, const uint8_t* data, std::size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  };

  auto response = ReadResponseStream(writer);
  if (response.result != DownloadResult::Success) {
    return {response.result, response.error, {}};
  }

  return {response.result, response.error, result};
}

esp_err_t HTTP::HTTPClient::Close() {
  return esp_http_client_close(m_handle);
}

esp_err_t HTTP::HTTPClient::Start(esp_http_client_method_t method, const char* url, int writeLen) {
  esp_err_t err;

  m_ratelimiter = HTTP::RateLimiters::GetRateLimiter(url);
  if (m_ratelimiter == nullptr) {
    OS_LOGW(TAG, "Invalid URL!");
    return ESP_FAIL;
  }
  if (!m_ratelimiter->tryRequest()) {
    OS_LOGW(TAG, "Hit ratelimit, refusing to send request!");
    return ESP_FAIL;
  }

  err = esp_http_client_set_url(m_handle, url);
  if (err != ESP_OK) return err;

  err = esp_http_client_set_method(m_handle, HTTP_METHOD_GET);
  if (err != ESP_OK) return err;

  return esp_http_client_open(m_handle, writeLen);
}

esp_err_t HTTP::HTTPClient::EventHandler(esp_http_client_event_t* evt) {
  HTTPClient* client = reinterpret_cast<HTTPClient*>(evt->user_data);

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
    OS_LOGI(TAG, "Got header_received event: %s - %s", evt->header_key, evt->header_value);
    return client->HandleHeader(evt->header_key, evt->header_value);
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

esp_err_t HTTP::HTTPClient::HandleHeader(std::string_view key, std::string_view value) {
  if (key == "Retry-After") {
    // TODO: Set block on m_ratelimiter
  }

  return ESP_OK;
}
