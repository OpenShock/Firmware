#pragma once

#include "Common.h"
#include "http/HTTPClientState.h"
#include "http/HTTPResponse.h"
#include "http/JsonResponse.h"
#include "RateLimiter.h"

#include <esp_err.h>

#include <cstdint>
#include <memory>

namespace OpenShock::HTTP {
  class HTTPClient {
    DISABLE_COPY(HTTPClient);
    DISABLE_MOVE(HTTPClient);

  public:
    HTTPClient(const char* url, uint32_t timeoutMs = 10'000)
      : m_state(std::make_shared<HTTPClientState>(url, timeoutMs))
    {
    }

    inline esp_err_t SetUrl(const char* url) {
      return m_state->SetUrl(url);
    }

    inline esp_err_t SetHeader(const char* key, const char* value) {
      return m_state->SetHeader(key, value);
    }

    inline HTTPResponse Get() {
      auto response = m_state->StartRequest(HTTP_METHOD_GET, 0);
      if (response.error != HTTPError::None) return HTTP::HTTPResponse(response.error, response.retryAfterSeconds);

      return HTTP::HTTPResponse(m_state, response.statusCode, response.contentLength, std::move(response.headers));
    }
    template<typename T>
    inline JsonResponse<T> GetJson(JsonParserFn<T> jsonParser) {
      auto response = m_state->StartRequest(HTTP_METHOD_GET, 0);
      if (response.error != HTTPError::None) return HTTP::JsonResponse<T>(response.error, response.retryAfterSeconds);

      return HTTP::JsonResponse(m_state, jsonParser, response.statusCode, response.contentLength, std::move(response.headers));
    }

    inline esp_err_t Close() {
      return m_state->Close();
    }
  private:
    std::shared_ptr<HTTPClientState> m_state;
  };
}  // namespace OpenShock::HTTP
