#include "http/HTTPRequestManager.h"

#include "Time.h"

#include <HTTPClient.h>

#include <algorithm>
#include <numeric>
#include <unordered_map>

const char* const TAG                    = "HTTPRequestManager";
const char* const OPENSHOCK_FW_USERAGENT = OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (Espressif; " OPENSHOCK_FW_CHIP "; " OPENSHOCK_FW_BOARD ") " OPENSHOCK_FW_COMMIT;

struct RateLimit {
  RateLimit(std::uint16_t limitSeconds10, std::uint16_t limitSeconds30, std::uint16_t limitMinutes1, std::uint16_t limitMinutes5)
    : windowBegin(0), tooManyRequestsTime(0), requests {0}, index(0), m_limitSeconds10(limitSeconds10), m_limitSeconds30(limitSeconds30), m_limitMinutes1(limitMinutes1), m_limitMinutes5(limitMinutes5) { }

  std::int64_t windowBegin;
  std::int64_t tooManyRequestsTime;
  std::array<std::uint8_t, 50> requests;  // 10 second bucket, 5 minute window
  std::uint8_t index;

  std::uint8_t current() const { return requests[index]; }
  std::uint8_t total() const { return std::accumulate(requests.begin(), requests.end(), 0); }

private:
  std::uint16_t m_limitSeconds10;
  std::uint16_t m_limitSeconds30;
  std::uint16_t m_limitMinutes1;
  std::uint16_t m_limitMinutes5;
};

std::unordered_map<std::string, RateLimit> s_rateLimits;

using namespace OpenShock;

RateLimit _rateLimitFactory(const char* url) {
  if (strcmp(url, OPENSHOCK_API_DOMAIN) == 0) {
    return RateLimit(10, 30, 6, 30);
  }

  return RateLimit(10, 30, 60, 300);  // Default to 1 per second
}

bool _rateLimit(const char* url) {
  auto it = s_rateLimits.find(url);
  if (it == s_rateLimits.end()) {
    s_rateLimits.emplace(url, _rateLimitFactory(url));
    it = s_rateLimits.find(url);
  }

  RateLimit& rateLimit = it->second;

  if (rateLimit.tooManyRequestsTime > 0) {
    // If it's been more than 30 seconds since we were rate limited by the server, reset the rate limit
    if (OpenShock::millis() - rateLimit.tooManyRequestsTime > 30 * 1000) {
      rateLimit.tooManyRequestsTime = 0;
    } else {
      return true;
    }
  }

  std::int64_t now = OpenShock::millis();
  if (now - rateLimit.windowBegin > 30 * 60 * 1000) {
    rateLimit.windowBegin = now;
    rateLimit.index       = 0;
    rateLimit.requests.fill(0);
  }

  rateLimit.requests[rateLimit.index]++;
  rateLimit.index++;

  return false;
}

void _registerTooManyRequests(const char* url) {
  auto it = s_rateLimits.find(url);
  if (it == s_rateLimits.end()) {
    s_rateLimits.emplace(url, _rateLimitFactory(url));
    it = s_rateLimits.find(url);
  }

  RateLimit& rateLimit = it->second;

  rateLimit.tooManyRequestsTime = OpenShock::millis();
}

void _setupClient(HTTPClient& client) {
  client.setUserAgent(OPENSHOCK_FW_USERAGENT);
}
HTTP::Response<String> _doGet(HTTPClient& client, const char* url, const std::map<String, String>& headers) {
  if (!client.begin(url)) {
    ESP_LOGE(TAG, "Failed to begin HTTP request");
    return {HTTP::RequestResult::RequestFailed, 0, ""};
  }

  for (auto& header : headers) {
    client.addHeader(header.first, header.second);
  }

  int responseCode = client.GET();

  if (responseCode == HTTP_CODE_TOO_MANY_REQUESTS) {
    _registerTooManyRequests(url);
    return {HTTP::RequestResult::RateLimited, responseCode, ""};
  }

  if (responseCode == 418) {
    ESP_LOGW(TAG, "The server refused to brew coffee because it is, permanently, a teapot.");
  }

  return {HTTP::RequestResult::Success, responseCode, client.getString()};
}

HTTP::Response<String> HTTP::GetString(const char* url, const std::map<String, String>& headers, std::vector<int> acceptedCodes) {
  if (_rateLimit(url)) {
    return {RequestResult::RateLimited, 0, ""};
  }

  HTTPClient client;
  _setupClient(client);

  auto response = _doGet(client, url, headers);
  if (response.result != RequestResult::Success) {
    return response;
  }

  if (std::find(acceptedCodes.begin(), acceptedCodes.end(), response.code) == acceptedCodes.end()) {
    ESP_LOGE(TAG, "Received unexpected response code %d", response.code);
    return {RequestResult::CodeRejected, response.code, ""};
  }

  return response;
}
