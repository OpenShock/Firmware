#include "http/HTTPClient.h"

const char* const TAG = "HTTPClient";

#include "Convert.h"
#include "http/RateLimiters.h"
#include "Logging.h"
#include "util/DomainUtils.h"

using namespace OpenShock;

HTTP::HTTPResponse HTTP::HTTPClient::Get(const char* url) {
  esp_err_t err = m_state->StartRequest(HTTP_METHOD_GET, url, 0);
  if (err != ESP_OK) {
    // TODO: Do something
  }

  m_responseLength = esp_http_client_fetch_headers(m_handle);
  if (m_responseLength == ESP_FAIL) {
    return ESP_FAIL;
  }

  m_statusCode = esp_http_client_get_status_code(m_handle);

  return ESP_OK;
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

  return m_state

  return esp_http_client_open(m_handle, writeLen);
}

esp_err_t HTTP::HTTPClient::EventHandler(esp_http_client_event_t* evt) {
}

esp_err_t HTTP::HTTPClient::HandleHeader(std::string_view key, std::string_view value) {
  if (key == "Retry-After" && m_ratelimiter != nullptr) {
    uint32_t seconds;
    if (!Convert::ToUint32(value, seconds) || seconds <= 0) {
      seconds = 15;
    }

    OS_LOGI(TAG, "Retry-After: %d seconds, applying delay to rate limiter", seconds);
    m_ratelimiter->blockFor(seconds * 1000U);
  }

  return ESP_OK;
}
