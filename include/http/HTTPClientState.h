#pragma once

#include "Common.h"
#include "http/DownloadCallback.h"

#include <esp_http_client.h>

#include <string>
#include <string_view>
#include <vector>

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

    struct [[nodiscard]] StartRequestResult {
      esp_err_t err;
      bool isChunked;
      uint32_t nAvailable;
    };

    StartRequestResult StartRequest(esp_http_client_method_t method, const char* url, int writeLen);

    enum class ReadResultCode {
      Success           = 0,
      ConnectionClosed  = 1,
      NetworkError      = 2,
      SizeLimitExceeded = 3,
      Aborted           = 4,
    };

    struct [[nodiscard]] ReadResult {
      ReadResultCode result;
      std::size_t nRead;
    };

    // High-throughput streaming logic
    ReadResult ReadStreamImpl(DownloadCallback cb);
  private:
    static esp_err_t EventHandler(esp_http_client_event_t* evt);
    esp_err_t EventHeaderHandler(std::string_view key, std::string_view value);

    esp_http_client_handle_t m_handle;
    bool m_reading;
    std::vector<std::pair<std::string, std::string>> m_headers;
  };
} // namespace OpenShock::HTTP
