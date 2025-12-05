#pragma once

#include "Common.h"
#include "http/DownloadCallback.h"
#include "http/HTTPError.h"
#include "http/JsonParserFn.h"
#include "http/ReadResult.h"

#include <cJSON.h>

#include <esp_http_client.h>

#include <map>
#include <string>
#include <string_view>

namespace OpenShock::HTTP {
  class HTTPClientState {
    DISABLE_COPY(HTTPClientState);
    DISABLE_MOVE(HTTPClientState);
  public:
    HTTPClientState(uint32_t timeoutMs);
    ~HTTPClientState();

    inline esp_err_t SetHeader(const char* key, const char* value) {
      return esp_http_client_set_header(m_handle, key, value);
    }

    struct HeaderEntry {
      std::string key;
      std::string value;
    };

    struct [[nodiscard]] StartRequestResult {
      HTTPError error{};
      uint32_t retryAfterSeconds{};
      uint16_t statusCode{};
      bool isChunked{};
      uint32_t contentLength{};
      std::map<std::string, std::string> headers{};
    };

    StartRequestResult StartRequest(esp_http_client_method_t method, const char* url, int writeLen);

    // High-throughput streaming logic
    ReadResult<uint32_t> ReadStreamImpl(DownloadCallback cb);

    ReadResult<std::string> ReadStringImpl(uint32_t reserve);

    template<typename T>
    inline ReadResult<T> ReadJsonImpl(uint32_t reserve, JsonParserFn<T> jsonParser)
    {
      auto response = ReadStringImpl(reserve);
      if (response.error != HTTPError::None) {
        return response.error;
      }

      cJSON* json = cJSON_ParseWithLength(response.data.c_str(), response.data.length());
      if (json == nullptr) {
        return HTTPError::ParseFailed;
      }

      T data;
      if (!jsonParser(json, data)) {
        return HTTPError::ParseFailed;
      }

      cJSON_Delete(json);

      return data;
    }

    inline esp_err_t Close() {
      return esp_http_client_close(m_handle);
    }
  private:
    static esp_err_t EventHandler(esp_http_client_event_t* evt);
    esp_err_t EventHeaderHandler(std::string key, std::string value);

    esp_http_client_handle_t m_handle;
    bool m_reading;
    uint32_t m_retryAfterSeconds;
    std::map<std::string, std::string> m_headers;
  };
} // namespace OpenShock::HTTP
