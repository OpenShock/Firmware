#include <freertos/FreeRTOS.h>

#include "http/HTTPClient.h"

const char* const TAG = "HTTPClient";

#include "Convert.h"
#include "http/RateLimiters.h"
#include "Logging.h"
#include "util/DomainUtils.h"

using namespace OpenShock;

HTTP::HTTPClient::InternalResult HTTP::HTTPClient::GetInternal(const char* url) {
  auto ratelimiter = HTTP::RateLimiters::GetRateLimiter(url);
  if (ratelimiter == nullptr) {
    OS_LOGW(TAG, "Invalid URL!");
    return {HTTPError::InvalidUrl, {}};
  }

  if (!ratelimiter->tryRequest()) {
    OS_LOGW(TAG, "Hit ratelimit, refusing to send request!");
    return {HTTPError::RateLimited, {}};
  }


  auto result = m_state->StartRequest(HTTP_METHOD_GET, url, 0);
  if (auto error = std::get_if<HTTPError>(&result)) {
    return {*error, {}};
  }

  return {HTTPError::None, std::get<HTTPClientState::StartRequestResult>(result)};
}
