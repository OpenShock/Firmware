#include <freertos/FreeRTOS.h>

#include "http/HTTPClient.h"

const char* const TAG = "HTTPClient";

#include "Convert.h"
#include "http/RateLimiters.h"
#include "Logging.h"
#include "util/DomainUtils.h"

using namespace OpenShock;

HTTP::HTTPClientState::StartRequestResult HTTP::HTTPClient::GetInternal(const char* url) {
  auto ratelimiter = HTTP::RateLimiters::GetRateLimiter(url);
  if (ratelimiter == nullptr) {
    OS_LOGW(TAG, "Invalid URL!");
    return { .error = HTTPError::InvalidUrl };
  }

  if (!ratelimiter->tryRequest()) {
    OS_LOGW(TAG, "Hit ratelimit, refusing to send request!");
    return { .error = HTTPError::RateLimited };
  }


  return m_state->StartRequest(HTTP_METHOD_GET, url, 0);
}
