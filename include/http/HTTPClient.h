#pragma once

#include "Common.h"
#include "RateLimiter.h"

#include <cJSON.h>

#include <esp_http_client.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace OpenShock::HTTP {
  enum class DownloadResult : uint8_t {
    Closed,       // Connection closed
    Success,      // Request completed successfully
    TimedOut,     // Request timed out
    ParseFailed,  // Request completed, but JSON parsing failed
    Cancelled,    // Request was cancelled
  };

  template<typename T>
  struct [[nodiscard]] Response {
    DownloadResult result;
    esp_err_t error;
    T data;
  };

  template<typename T>
  using JsonParser               = std::function<bool(const cJSON* json, T& data)>;
  using GotContentLengthCallback = std::function<bool(int contentLength)>;
  using DownloadCallback         = std::function<bool(std::size_t offset, const uint8_t* data, std::size_t len)>;

  class HTTPClient {
    DISABLE_COPY(HTTPClient);
    DISABLE_MOVE(HTTPClient);

  public:
    HTTPClient(uint32_t timeoutMs = 10'000, const char* useragent = OpenShock::Constants::FW_USERAGENT);
    ~HTTPClient();

    inline esp_err_t SetHeader(const char* key, const char* value) {
      return esp_http_client_set_header(m_handle, key, value);
    }
    esp_err_t SetHeaders(const std::map<std::string, std::string>& headers);

    esp_err_t Get(const char* url);

    inline int ResponseLength() const {
      return m_responseLength;
    }
    inline int StatusCode() const {
      return m_statusCode;
    }

    Response<std::size_t> ReadResponseStream(DownloadCallback downloadCallback);
    Response<std::string> ReadResponseString();
    template<typename T>
    inline Response<T> ReadResponseJSON(JsonParser<T> jsonParser)
    {
      auto response = ReadResponseString();
      if (response.result != DownloadResult::Success) {
        return {response.result, response.error, {}};
      }

      cJSON* json = cJSON_ParseWithLength(response.data.c_str(), response.data.length());
      if (json == nullptr) {
        return {DownloadResult::ParseFailed, ESP_OK, {}};
      }

      T data;
      if (!jsonParser(json, data)) {
        return {DownloadResult::ParseFailed, ESP_OK, {}};
      }

      cJSON_Delete(json);

      return {response.result, ESP_OK, std::move(data)};
    }

    esp_err_t Close();
  private:
    esp_err_t Start(esp_http_client_method_t method, const char* url, int writeLen);

    static esp_err_t EventHandler(esp_http_client_event_t* evt);
    esp_err_t HandleHeader(std::string_view key, std::string_view value);

    esp_http_client_handle_t m_handle;
    std::shared_ptr<RateLimiter> m_ratelimiter;
    bool m_connected;
    int m_responseLength;
    int m_statusCode;

    GotContentLengthCallback m_cbGotContentLength;
    DownloadCallback m_cbDownload;
  };
}  // namespace OpenShock::HTTP
