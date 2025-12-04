#pragma once

#include "Common.h"
#include "http/HTTPClientState.h"

#include <memory>

namespace OpenShock::HTTP {
  class HTTPClient;
  class HTTPResponse {
    DISABLE_COPY(HTTPResponse);
    DISABLE_MOVE(HTTPResponse);

    friend HTTPClient;

    HTTPResponse(std::shared_ptr<HTTPClientState> state);
  public:

  private:
    std::weak_ptr<HTTPClientState> m_state;
  };
} // namespace OpenShock::HTTP
