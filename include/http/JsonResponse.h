#pragma once

#include "Common.h"
#include "http/DownloadCallback.h"
#include "http/HTTPClientState.h"
#include "http/JsonParserFn.h"
#include "http/ReadResult.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace OpenShock::HTTP {
  class HTTPClient;
  template<typename T>
  class [[nodiscard]] JsonResponse {
    DISABLE_DEFAULT(JsonResponse);
    DISABLE_COPY(JsonResponse);
    DISABLE_MOVE(JsonResponse);

    friend class HTTPClient;

    JsonResponse(std::shared_ptr<HTTPClientState> state, JsonParserFn<T> jsonParser, uint16_t statusCode, uint32_t contentLength, std::map<std::string, std::string> headers)
      : m_state(state)
      , m_jsonParser(jsonParser)
      , m_error(HTTPError::None)
      , m_retryAfterSeconds(0)
      , m_statusCode(statusCode)
      , m_contentLength(contentLength)
      , m_headers(std::move(headers))
    {
    }
  public:
    JsonResponse(HTTPError error)
      : m_state()
      , m_jsonParser()
      , m_error(error)
      , m_retryAfterSeconds(0)
      , m_statusCode(0)
      , m_contentLength(0)
      , m_headers()
    {
    }
    JsonResponse(HTTPError error, uint32_t retryAfterSeconds)
      : m_state()
      , m_jsonParser()
      , m_error(error)
      , m_retryAfterSeconds(retryAfterSeconds)
      , m_statusCode(0)
      , m_contentLength(0)
      , m_headers()
    {
    }

    inline bool Ok() const { return m_error == HTTPError::None && !m_state.expired(); }
    inline HTTPError Error() const { return m_error; }
    inline uint32_t RetryAfterSeconds() const { return m_retryAfterSeconds; }
    inline uint16_t StatusCode() const { return m_statusCode; }
    inline uint32_t ContentLength() const { return m_contentLength; }

    inline ReadResult<T> ReadJson()
    {
      auto locked = m_state.lock();
      if (locked == nullptr) return HTTPError::ConnectionClosed;

      return locked->ReadJsonImpl(m_contentLength, m_jsonParser);
    }
  private:
    std::weak_ptr<HTTPClientState> m_state;
    JsonParserFn<T> m_jsonParser;
    HTTPError m_error;
    uint32_t m_retryAfterSeconds;
    uint16_t m_statusCode;
    uint32_t m_contentLength;
    std::map<std::string, std::string> m_headers;
  };
} // namespace OpenShock::HTTP
