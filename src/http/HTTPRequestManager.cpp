#include "http/HTTPRequestManager.h"

#include "Time.h"

#include <HTTPClient.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <vector>

constexpr std::size_t HTTP_BUFFER_SIZE = 4096LLU;
constexpr int HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

const char* const TAG                    = "HTTPRequestManager";
const char* const OPENSHOCK_FW_USERAGENT = OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (Espressif; " OPENSHOCK_FW_CHIP "; " OPENSHOCK_FW_BOARD ") " OPENSHOCK_FW_COMMIT;

struct RateLimit {
  RateLimit() : m_blockUntilMs(0), m_limits(), m_requests() { }

  void addLimit(std::uint32_t durationMs, std::uint16_t count) {
    // Insert sorted
    m_limits.insert(std::upper_bound(m_limits.begin(), m_limits.end(), durationMs, [](std::int64_t durationMs, const Limit& limit) { return durationMs > limit.durationMs; }), {durationMs, count});
  }
  void clearLimits() { m_limits.clear(); }

  bool tryRequest() {
    std::int64_t now = OpenShock::millis();

    if (m_blockUntilMs > now) {
      ESP_LOGW(TAG, "Rate limited for %lld more milliseconds", m_blockUntilMs - now);
      return false;
    }

    // Remove all requests that are older than the biggest limit
    while (!m_requests.empty() && m_requests.front() < now - m_limits.back().durationMs) {
      m_requests.erase(m_requests.begin());
    }

    // Check if we've exceeded any limits
    for (auto& limit : m_limits) {
      if (m_requests.size() >= limit.count) {
        m_blockUntilMs = now + limit.durationMs;
        ESP_LOGW(TAG, "Rate limited for %lld milliseconds", limit.durationMs);
        return false;
      }
    }

    // Add the request
    m_requests.push_back(now);

    return true;
  }
  void clearRequests() { m_requests.clear(); }

  void blockUntil(std::int64_t blockUntilMs) { m_blockUntilMs = blockUntilMs; }

  std::uint32_t requestsSince(std::int64_t sinceMs) {
    return std::count_if(m_requests.begin(), m_requests.end(), [sinceMs](std::int64_t requestMs) { return requestMs >= sinceMs; });
  }

private:
  struct Limit {
    std::int64_t durationMs;
    std::uint16_t count;
  };

  std::int64_t m_blockUntilMs;
  std::vector<Limit> m_limits;
  std::vector<std::int64_t> m_requests;
};

std::unordered_map<std::string, std::shared_ptr<RateLimit>> s_rateLimits;

using namespace OpenShock;

const char* _strfind(const char* haystack, const char* haystackEnd, const char* needle, std::size_t needleLen) {
  const char* needleEnd = needle + needleLen;
  const char* result    = std::search(haystack, haystackEnd, needle, needleEnd);
  if (result == haystackEnd) {
    return nullptr;
  }

  return result;
}

bool _getDomain(const char* url, char (&domain)[256]) {
  if (url == nullptr) {
    memset(domain, 0, 256);
    return false;
  }

  std::size_t urlLen = strlen(url);
  if (urlLen == 0) {
    memset(domain, 0, 256);
    return false;
  }

  const char* urlEnd = url + strlen(url);

  const char* ptr;

  // Get the beginning of the domain (after the protocol) eg. "https://api.example.com/path" -> "api.example.com/path"
  ptr = _strfind(url, urlEnd, "://", 3);
  if (ptr != nullptr) {
    url = ptr + 3;
  }

  // Get the end of the domain (before the first colon or slash) eg. "api.example.com/path" -> "api.example.com" or "api.example.com:8080/path" -> "api.example.com"
  ptr = std::find_if(url, urlEnd, [](char c) { return c == ':' || c == '/'; });
  if (ptr != urlEnd) {
    urlEnd = ptr;
  }

  // Reverse trough url, get domain seperator, then store subdomain seperator in ptr
  bool foundDomSep = false;
  for (ptr = urlEnd - 1; ptr != url; ptr--) {
    if (*ptr == '.') {
      if (foundDomSep) {
        url = ptr + 1;
        break;
      }
      foundDomSep = true;
    }
  }
  if (!foundDomSep) {
    return false;
  }

  // Copy the domain into the buffer, and set the null terminator
  memcpy(domain, url, urlEnd - url);
  domain[urlEnd - url] = '\0';

  return true;
}

std::shared_ptr<RateLimit> _rateLimitFactory(const char (&domain)[256]) {
  auto rateLimit = std::make_shared<RateLimit>();

  // Add default limits
  rateLimit->addLimit(1000, 5);        // 5 per second
  rateLimit->addLimit(10 * 1000, 10);  // 10 per 10 seconds

  // per-domain limits
  if (strcmp(domain, OPENSHOCK_API_DOMAIN) == 0) {
    rateLimit->addLimit(60 * 1000, 12);        // 12 per minute
    rateLimit->addLimit(60 * 60 * 1000, 120);  // 120 per hour
  }

  return rateLimit;
}

std::shared_ptr<RateLimit> _getRateLimiter(const char* url) {
  char domain[256];
  if (!_getDomain(url, domain)) {
    return nullptr;
  }

  ESP_LOGI(TAG, "Getting rate limiter for domain: %s", domain);

  auto it = s_rateLimits.find(domain);
  if (it == s_rateLimits.end()) {
    s_rateLimits.emplace(domain, _rateLimitFactory(domain));
    it = s_rateLimits.find(domain);
  }

  return it->second;
}

void _setupClient(HTTPClient& client) {
  client.setUserAgent(OPENSHOCK_FW_USERAGENT);
}

HTTP::Response<std::size_t> _doGetStream(
  HTTPClient& client,
  const char* url,
  const std::map<String, String>& headers,
  const std::vector<int>& acceptedCodes,
  std::shared_ptr<RateLimit> rateLimiter,
  HTTP::GotContentLengthCallback contentLengthCallback,
  HTTP::DownloadCallback downloadCallback
) {
  if (!client.begin(url)) {
    ESP_LOGE(TAG, "Failed to begin HTTP request");
    return {HTTP::RequestResult::RequestFailed, 0};
  }

  for (auto& header : headers) {
    client.addHeader(header.first, header.second);
  }

  int responseCode = client.GET();

  if (responseCode == HTTP_CODE_TOO_MANY_REQUESTS) {
    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Retry-After

    // Get "Retry-After" header
    String retryAfterStr = client.header("Retry-After");

    // Try to parse it as an integer (delay-seconds)
    long retryAfter = 0;
    if (retryAfterStr.length() > 0 && std::all_of(retryAfterStr.begin(), retryAfterStr.end(), isdigit)) {
      retryAfter = retryAfterStr.toInt();
    }

    // If header missing/unparseable, default to 15 seconds
    if (retryAfter <= 0) {
      retryAfter = 15;
    }

    // Get the block-until time
    std::int64_t blockUntilMs = OpenShock::millis() + retryAfter * 1000;

    // Apply the block-until time
    rateLimiter->blockUntil(blockUntilMs);

    return {HTTP::RequestResult::RateLimited, responseCode, 0};
  }

  if (responseCode == 418) {
    ESP_LOGW(TAG, "The server refused to brew coffee because it is, permanently, a teapot.");
  }

  if (std::find(acceptedCodes.begin(), acceptedCodes.end(), responseCode) == acceptedCodes.end()) {
    ESP_LOGE(TAG, "Received unexpected response code %d", responseCode);
    return {HTTP::RequestResult::CodeRejected, responseCode, 0};
  }

  int contentLength = client.getSize();
  if (contentLength == 0) {
    return {HTTP::RequestResult::Success, responseCode, 0};
  }

  if (contentLength > 0) {
    if (!contentLengthCallback(contentLength)) {
      return {HTTP::RequestResult::Cancelled, responseCode, 0};
    }
  } else {
    ESP_LOGI(TAG, "Content-Length header missing, using chunked transfer encoding");
    contentLength = HTTP_DOWNLOAD_SIZE_LIMIT;  // Set a limit to prevent downloading too much
  }

  WiFiClient* stream = client.getStreamPtr();
  if (stream == nullptr) {
    ESP_LOGE(TAG, "Failed to get stream");
    return {HTTP::RequestResult::RequestFailed, 0};
  }

  std::size_t nWritten = 0;

  std::vector<uint8_t> buffer;
  buffer.resize(HTTP_BUFFER_SIZE);
  while (client.connected() && nWritten < contentLength) {
    std::size_t bytesAvailable = stream->available();
    if (bytesAvailable == 0) {
      ESP_LOGD(TAG, "No bytes available");
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    std::size_t bytesToRead = std::min(bytesAvailable, HTTP_BUFFER_SIZE);

    std::size_t bytesRead = stream->readBytes(buffer.data(), bytesToRead);

    if (!downloadCallback(nWritten, buffer.data(), bytesRead)) {
      return {HTTP::RequestResult::Cancelled, responseCode, nWritten};
    }

    nWritten += bytesRead;

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  return {HTTP::RequestResult::Success, responseCode, nWritten};
}

HTTP::Response<std::size_t> HTTP::Download(const char* const url, const std::map<String, String>& headers, HTTP::GotContentLengthCallback contentLengthCallback, HTTP::DownloadCallback downloadCallback, const std::vector<int>& acceptedCodes) {
  std::shared_ptr<RateLimit> rateLimiter = _getRateLimiter(url);
  if (rateLimiter == nullptr) {
    return {RequestResult::InvalidURL, 0, 0};
  }

  if (!rateLimiter->tryRequest()) {
    return {RequestResult::RateLimited, 0, 0};
  }

  HTTPClient client;
  _setupClient(client);

  auto response = _doGetStream(client, url, headers, acceptedCodes, rateLimiter, contentLengthCallback, downloadCallback);
  if (response.result != RequestResult::Success) {
    return response;
  }

  if (std::find(acceptedCodes.begin(), acceptedCodes.end(), response.code) == acceptedCodes.end()) {
    ESP_LOGE(TAG, "Received unexpected response code %d", response.code);
    return {RequestResult::CodeRejected, response.code, 0};
  }

  return response;
}

HTTP::Response<std::string> HTTP::GetString(const char* const url, const std::map<String, String>& headers, const std::vector<int>& acceptedCodes) {
  std::string result;

  auto allocator = [&result](std::size_t contentLength) {
    result.reserve(contentLength);
    return true;
  };
  auto writer = [&result](std::size_t offset, const uint8_t* data, std::size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  };

  auto response = Download(url, headers, allocator, writer, acceptedCodes);

  if (response.result != RequestResult::Success) {
    return {response.result, response.code, {}};
  }

  return {response.result, response.code, result};
}
