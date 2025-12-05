#pragma once

#include "Common.h"
#include "http/DownloadCallback.h"
#include "http/HTTPClientState.h"
#include "http/JsonParserFn.h"
#include "http/ReadResult.h"

#include <cJSON.h>

#include <cstdint>
#include <memory>
#include <string>

namespace OpenShock::HTTP {
  class HTTPClient;
  class [[nodiscard]] HTTPResponse {
    DISABLE_DEFAULT(HTTPResponse);
    DISABLE_COPY(HTTPResponse);
    DISABLE_MOVE(HTTPResponse);

    friend class HTTPClient;

    HTTPResponse(std::shared_ptr<HTTPClientState> state, int statusCode, uint32_t contentLength)
      : m_state(state)
      , m_error(HTTPError::None)
      , m_statusCode(statusCode)
      , m_contentLength(contentLength)
    {
    }
  public:
    HTTPResponse(HTTPError error)
      : m_state()
      , m_error(error)
      , m_statusCode(0)
      , m_contentLength(0)
    {
    }

    inline bool Ok() const { return m_error == HTTPError::None && !m_state.expired(); }
    inline HTTPError Error() const { return m_error; }
    inline uint32_t StatusCode() const { return m_statusCode; }
    inline uint32_t ContentLength() const { return m_contentLength; }

    inline ReadResult<uint32_t> ReadStream(DownloadCallback downloadCallback) {
      auto locked = m_state.lock();
      if (locked == nullptr) return HTTPError::ConnectionClosed;

      return locked->ReadStreamImpl(downloadCallback);
    }

    inline ReadResult<std::string> ReadString() {
      auto locked = m_state.lock();
      if (locked == nullptr) return HTTPError::ConnectionClosed;

      return locked->ReadStringImpl(m_contentLength);
    }

    template<typename T>
    inline ReadResult<T> ReadJson(JsonParserFn<T> jsonParser)
    {
      auto locked = m_state.lock();
      if (locked == nullptr) return HTTPError::ConnectionClosed;

      return locked->ReadJsonImpl(m_contentLength, jsonParser);
    }
  private:
    std::weak_ptr<HTTPClientState> m_state;
    HTTPError m_error;
    uint32_t m_statusCode;
    uint32_t m_contentLength;
  };
} // namespace OpenShock::HTTP
