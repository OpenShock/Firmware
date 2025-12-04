#pragma once

#include "Common.h"
#include "http/HTTPClientState.h"
#include "http/HTTPResponse.h"

#include <esp_http_client.h>

#include <memory>
#include <string>
#include <string_view>

namespace OpenShock::HTTP {
  class HTTPClient {
    DISABLE_COPY(HTTPClient);
    DISABLE_MOVE(HTTPClient);

  public:
    HTTPClient(uint32_t timeoutMs = 10'000)
      : m_state(std::make_shared<HTTPClientState>())
    {
    }

    inline esp_err_t SetHeader(const char* key, const char* value) {
      return m_state->SetHeader(key, value);
    }

    HTTPResponse Get(const char* url);

    esp_err_t Close();
  private:
    esp_err_t Start(esp_http_client_method_t method, const char* url, int writeLen);

    std::shared_ptr<HTTPClientState> m_state;
  };
}  // namespace OpenShock::HTTP
