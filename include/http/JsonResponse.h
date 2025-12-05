#pragma once

#include "Common.h"
#include "http/DownloadCallback.h"
#include "http/HTTPClientState.h"
#include "http/JsonParserFn.h"
#include "http/ReadResult.h"

#include <cstdint>
#include <memory>

namespace OpenShock::HTTP {
  class HTTPClient;
  template<typename T>
  class [[nodiscard]] JsonResponse {
    DISABLE_DEFAULT(JsonResponse);
    DISABLE_COPY(JsonResponse);
    DISABLE_MOVE(JsonResponse);

    friend class HTTPClient;

    JsonResponse(std::shared_ptr<HTTPClientState> state, JsonParserFn<T> jsonParser, uint16_t statusCode, uint32_t contentLength)
      : m_state(state)
      , m_jsonParser(jsonParser)
      , m_error(HTTPError::None)
      , m_statusCode(statusCode)
      , m_contentLength(contentLength)
    {
    }
  public:
    JsonResponse(HTTPError error)
      : m_state()
      , m_jsonParser()
      , m_error(error)
      , m_statusCode(0)
      , m_contentLength(0)
    {
    }

    inline bool Ok() const { return m_error == HTTPError::None && !m_state.expired(); }
    inline HTTPError Error() const { return m_error; }
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
    uint16_t m_statusCode;
    uint32_t m_contentLength;
  };
} // namespace OpenShock::HTTP
