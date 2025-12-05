#pragma once

#include "Common.h"
#include "http/HTTPClientState.h"
#include "http/HTTPResponse.h"
#include "http/JsonResponse.h"

#include <esp_http_client.h>

#include <memory>

namespace OpenShock::HTTP {
  class HTTPClient {
    DISABLE_COPY(HTTPClient);
    DISABLE_MOVE(HTTPClient);

  public:
    HTTPClient(uint32_t timeoutMs = 10'000)
      : m_state(std::make_shared<HTTPClientState>(timeoutMs))
    {
    }

    inline esp_err_t SetHeader(const char* key, const char* value) {
      return m_state->SetHeader(key, value);
    }

    inline HTTPResponse Get(const char* url) {
      auto response = GetInternal(url);
      if (response.error != HTTPError::None) return response.error;

      return HTTP::HTTPResponse(m_state, response.data.statusCode, response.data.contentLength);
    }
    template<typename T>
    inline JsonResponse<T> GetJson(const char* url, JsonParserFn<T> jsonParser) {
      auto response = GetInternal(url);
      if (response.error != HTTPError::None) return response.error;

      return HTTP::JsonResponse(m_state, jsonParser, response.data.statusCode, response.data.contentLength);
    }

    inline esp_err_t Close() {
      return m_state->Close();
    }
  private:
    struct InternalResult {
      HTTPError error;
      HTTPClientState::StartRequestResult data;
    };
    InternalResult GetInternal(const char* url);

    std::shared_ptr<HTTPClientState> m_state;
  };
}  // namespace OpenShock::HTTP
