#include "http/HTTPRequestManager.h"

#include "Common.h"
#include "Time.h"

#include <HTTPClient.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <vector>

const std::size_t HTTP_BUFFER_SIZE = 4096LLU;
const int HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

const char* const TAG = "HTTPRequestManager";

struct RateLimit {
  RateLimit() : m_mutex(xSemaphoreCreateMutex()), m_blockUntilMs(0), m_limits(), m_requests() { }

  void addLimit(std::uint32_t durationMs, std::uint16_t count) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    // Insert sorted
    m_limits.insert(std::upper_bound(m_limits.begin(), m_limits.end(), durationMs, [](std::int64_t durationMs, const Limit& limit) { return durationMs > limit.durationMs; }), {durationMs, count});

    xSemaphoreGive(m_mutex);
  }
  void clearLimits() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    m_limits.clear();

    xSemaphoreGive(m_mutex);
  }

  bool tryRequest() {
    std::int64_t now = OpenShock::millis();

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    if (m_blockUntilMs > now) {
      xSemaphoreGive(m_mutex);
      return false;
    }

    // Remove all requests that are older than the biggest limit
    while (!m_requests.empty() && m_requests.front() < now - m_limits.back().durationMs) {
      m_requests.erase(m_requests.begin());
    }

    // Check if we've exceeded any limits
    auto it = std::find_if(m_limits.begin(), m_limits.end(), [this](const RateLimit::Limit& limit) { return m_requests.size() >= limit.count; });
    if (it != m_limits.end()) {
      m_blockUntilMs = now + it->durationMs;
      xSemaphoreGive(m_mutex);
      return false;
    }

    // Add the request
    m_requests.push_back(now);

    xSemaphoreGive(m_mutex);

    return true;
  }
  void clearRequests() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_requests.clear();
    xSemaphoreGive(m_mutex);
  }

  void blockUntil(std::int64_t blockUntilMs) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_blockUntilMs = blockUntilMs;
    xSemaphoreGive(m_mutex);
  }

  std::uint32_t requestsSince(std::int64_t sinceMs) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    std::uint32_t result = std::count_if(m_requests.begin(), m_requests.end(), [sinceMs](std::int64_t requestMs) { return requestMs >= sinceMs; });
    xSemaphoreGive(m_mutex);
    return result;
  }

private:
  struct Limit {
    std::int64_t durationMs;
    std::uint16_t count;
  };

  SemaphoreHandle_t m_mutex;
  std::int64_t m_blockUntilMs;
  std::vector<Limit> m_limits;
  std::vector<std::int64_t> m_requests;
};

SemaphoreHandle_t s_rateLimitsMutex = xSemaphoreCreateMutex();
std::unordered_map<std::string, std::shared_ptr<RateLimit>> s_rateLimits;

using namespace OpenShock;

StringView _getDomain(StringView url) {
  if (url.isNullOrEmpty()) {
    return StringView::Null();
  }

  // Remove the protocol, port, and path eg. "https://api.example.com:443/path" -> "api.example.com"
  url = url.afterDelimiter("://"_sv).beforeDelimiter('/').beforeDelimiter(':');

  // Remove all subdomains eg. "api.example.com" -> "example.com"
  auto domainSep = url.rfind('.');
  if (domainSep == StringView::npos) {
    return url;  // E.g. "localhost"
  }
  domainSep = url.rfind('.', domainSep - 1);
  if (domainSep != StringView::npos) {
    url = url.substr(domainSep + 1);
  }

  return url;
}

std::shared_ptr<RateLimit> _rateLimitFactory(StringView domain) {
  auto rateLimit = std::make_shared<RateLimit>();

  // Add default limits
  rateLimit->addLimit(1000, 5);        // 5 per second
  rateLimit->addLimit(10 * 1000, 10);  // 10 per 10 seconds

  // per-domain limits
  if (domain == OPENSHOCK_API_DOMAIN) {
    rateLimit->addLimit(60 * 1000, 12);        // 12 per minute
    rateLimit->addLimit(60 * 60 * 1000, 120);  // 120 per hour
  }

  return rateLimit;
}

std::shared_ptr<RateLimit> _getRateLimiter(StringView url) {
  auto domain = _getDomain(url).toString();
  if (domain.empty()) {
    return nullptr;
  }

  xSemaphoreTake(s_rateLimitsMutex, portMAX_DELAY);

  auto it = s_rateLimits.find(domain);
  if (it == s_rateLimits.end()) {
    s_rateLimits.emplace(domain, _rateLimitFactory(domain));
    it = s_rateLimits.find(domain);
  }

  xSemaphoreGive(s_rateLimitsMutex);

  return it->second;
}

void _setupClient(HTTPClient& client) {
  client.setUserAgent(OpenShock::Constants::FW_USERAGENT);
}

struct StreamReaderResult {
  HTTP::RequestResult result;
  std::size_t nWritten;
};

constexpr bool _isCRLF(const uint8_t* buffer) {
  return buffer[0] == '\r' && buffer[1] == '\n';
}
constexpr bool _tryFindCRLF(std::size_t& pos, const uint8_t* buffer, std::size_t len) {
  const std::uint8_t* cur = buffer;
  const std::uint8_t* end = buffer + len - 1;

  while (cur < end) {
    if (_isCRLF(cur)) {
      pos = static_cast<std::size_t>(cur - buffer);
      return true;
    }

    ++cur;
  }

  return false;
}
constexpr bool _tryParseHexSizeT(std::size_t& result, StringView str) {
  if (str.isNullOrEmpty() || str.size() > sizeof(std::size_t) * 2) {
    return false;
  }

  result = 0;

  for (char c : str) {
    if (c >= '0' && c <= '9') {
      result = (result << 4) | (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      result = (result << 4) | (c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      result = (result << 4) | (c - 'A' + 10);
    } else {
      return false;
    }
  }

  return true;
}

enum ParserState : std::uint8_t {
  Ok,
  NeedMoreData,
  Invalid,
};

ParserState _parseChunkHeader(const std::uint8_t* buffer, std::size_t bufferLen, std::size_t& headerLen, std::size_t& payloadLen) {
  if (bufferLen < 5) {  // Bare minimum: "0\r\n\r\n"
    return ParserState::NeedMoreData;
  }

  // Find the first CRLF
  if (!_tryFindCRLF(headerLen, buffer, bufferLen)) {
    return ParserState::NeedMoreData;
  }

  // Header must have at least one character
  if (headerLen == 0) {
    ESP_LOGW(TAG, "Invalid chunk header length");
    return ParserState::Invalid;
  }

  // Check for end of size field (possibly followed by extensions which is separated by a semicolon)
  std::size_t sizeFieldEnd = headerLen;
  for (std::size_t i = 0; i < headerLen; ++i) {
    if (buffer[i] == ';') {
      sizeFieldEnd = i;
      break;
    }
  }

  // Bounds check
  if (sizeFieldEnd == 0 || sizeFieldEnd > 16) {
    ESP_LOGW(TAG, "Invalid chunk size field length");
    return ParserState::Invalid;
  }

  StringView sizeField(reinterpret_cast<const char*>(buffer), sizeFieldEnd);

  // Parse the chunk size
  if (!_tryParseHexSizeT(payloadLen, sizeField)) {
    ESP_LOGW(TAG, "Failed to parse chunk size");
    return ParserState::Invalid;
  }

  if (payloadLen > HTTP_DOWNLOAD_SIZE_LIMIT) {
    ESP_LOGW(TAG, "Chunk size too large");
    return ParserState::Invalid;
  }

  // Set the header length to the end of the CRLF
  headerLen += 2;

  return ParserState::Ok;
}

ParserState _parseChunk(const std::uint8_t* buffer, std::size_t bufferLen, std::size_t& payloadPos, std::size_t& payloadLen) {
  if (payloadPos == 0) {
    ParserState state = _parseChunkHeader(buffer, bufferLen, payloadPos, payloadLen);
    if (state != ParserState::Ok) {
      return state;
    }
  }

  std::size_t totalLen = payloadPos + payloadLen + 2;  // +2 for CRLF
  if (bufferLen < totalLen) {
    return ParserState::NeedMoreData;
  }

  // Check for CRLF
  if (!_isCRLF(buffer + totalLen - 2)) {
    ESP_LOGW(TAG, "Invalid chunk payload CRLF");
    return ParserState::Invalid;
  }

  return ParserState::Ok;
}

void _alignChunk(std::uint8_t* buffer, std::size_t& bufferCursor, std::size_t payloadPos, std::size_t payloadLen) {
  std::size_t totalLen  = payloadPos + payloadLen + 2;  // +2 for CRLF
  std::size_t remaining = bufferCursor - totalLen;
  if (remaining > 0) {
    memmove(buffer, buffer + totalLen, remaining);
    bufferCursor = remaining;
  } else {
    bufferCursor = 0;
  }
}

StreamReaderResult _readStreamDataChunked(HTTPClient& client, WiFiClient* stream, HTTP::DownloadCallback downloadCallback, std::int64_t begin, std::uint32_t timeoutMs) {
  std::size_t totalWritten   = 0;
  HTTP::RequestResult result = HTTP::RequestResult::Success;

  std::uint8_t* buffer = static_cast<std::uint8_t*>(malloc(HTTP_BUFFER_SIZE));
  if (buffer == nullptr) {
    ESP_LOGE(TAG, "Out of memory");
    return {HTTP::RequestResult::RequestFailed, 0};
  }

  ParserState state        = ParserState::NeedMoreData;
  std::size_t bufferCursor = 0, payloadPos = 0, payloadSize = 0;

  while (client.connected() && state != ParserState::Invalid) {
    if (begin + timeoutMs < OpenShock::millis()) {
      ESP_LOGW(TAG, "Request timed out");
      result = HTTP::RequestResult::TimedOut;
      break;
    }

    std::size_t bytesAvailable = stream->available();
    if (bytesAvailable == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    std::size_t bytesRead = stream->readBytes(buffer + bufferCursor, HTTP_BUFFER_SIZE - bufferCursor);
    if (bytesRead == 0) {
      ESP_LOGW(TAG, "No bytes read");
      result = HTTP::RequestResult::RequestFailed;
      break;
    }

    bufferCursor += bytesRead;

parseMore:
    state = _parseChunk(buffer, bufferCursor, payloadPos, payloadSize);
    if (state == ParserState::Invalid) {
      ESP_LOGE(TAG, "Failed to parse chunk");
      result = HTTP::RequestResult::RequestFailed;
      break;
    }
    ESP_LOGD(TAG, "Chunk parsed: %zu %zu", payloadPos, payloadSize);

    if (state == ParserState::NeedMoreData) {
      if (bufferCursor == HTTP_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Chunk too large");
        result = HTTP::RequestResult::RequestFailed;
        break;
      }
      continue;
    }

    // Check for zero chunk size (end of transfer)
    if (payloadSize == 0) {
      break;
    }

    if (!downloadCallback(totalWritten, buffer + payloadPos, payloadSize)) {
      result = HTTP::RequestResult::Cancelled;
      break;
    }

    totalWritten += payloadSize;
    _alignChunk(buffer, bufferCursor, payloadPos, payloadSize);
    payloadSize = 0;
    payloadPos  = 0;

    if (bufferCursor > 0) {
      goto parseMore;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }

  free(buffer);

  return {result, totalWritten};
}

StreamReaderResult _readStreamData(HTTPClient& client, WiFiClient* stream, std::size_t contentLength, HTTP::DownloadCallback downloadCallback, std::int64_t begin, std::uint32_t timeoutMs) {
  std::size_t nWritten       = 0;
  HTTP::RequestResult result = HTTP::RequestResult::Success;

  std::uint8_t* buffer = static_cast<std::uint8_t*>(malloc(HTTP_BUFFER_SIZE));

  while (client.connected() && nWritten < contentLength) {
    if (begin + timeoutMs < OpenShock::millis()) {
      ESP_LOGW(TAG, "Request timed out");
      result = HTTP::RequestResult::TimedOut;
      break;
    }

    std::size_t bytesAvailable = stream->available();
    if (bytesAvailable == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    std::size_t bytesToRead = std::min(bytesAvailable, HTTP_BUFFER_SIZE);

    std::size_t bytesRead = stream->readBytes(buffer, bytesToRead);
    if (bytesRead == 0) {
      ESP_LOGW(TAG, "No bytes read");
      result = HTTP::RequestResult::RequestFailed;
      break;
    }

    if (!downloadCallback(nWritten, buffer, bytesRead)) {
      ESP_LOGW(TAG, "Request cancelled by callback");
      result = HTTP::RequestResult::Cancelled;
      break;
    }

    nWritten += bytesRead;

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  free(buffer);

  return {result, nWritten};
}

HTTP::Response<std::size_t> _doGetStream(
  HTTPClient& client,
  StringView url,
  const std::map<String, String>& headers,
  const std::vector<int>& acceptedCodes,
  std::shared_ptr<RateLimit> rateLimiter,
  HTTP::GotContentLengthCallback contentLengthCallback,
  HTTP::DownloadCallback downloadCallback,
  std::uint32_t timeoutMs
) {
  std::int64_t begin = OpenShock::millis();
  if (!client.begin(url.toArduinoString())) {
    ESP_LOGE(TAG, "Failed to begin HTTP request");
    return {HTTP::RequestResult::RequestFailed, 0};
  }

  for (auto& header : headers) {
    client.addHeader(header.first, header.second);
  }

  int responseCode = client.GET();

  if (responseCode == HTTP_CODE_REQUEST_TIMEOUT || begin + timeoutMs < OpenShock::millis()) {
    ESP_LOGW(TAG, "Request timed out");
    return {HTTP::RequestResult::TimedOut, responseCode, 0};
  }

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
    if (contentLength > HTTP_DOWNLOAD_SIZE_LIMIT) {
      ESP_LOGE(TAG, "Content-Length too large");
      return {HTTP::RequestResult::RequestFailed, responseCode, 0};
    }

    if (!contentLengthCallback(contentLength)) {
      ESP_LOGW(TAG, "Request cancelled by callback");
      return {HTTP::RequestResult::Cancelled, responseCode, 0};
    }
  }

  WiFiClient* stream = client.getStreamPtr();
  if (stream == nullptr) {
    ESP_LOGE(TAG, "Failed to get stream");
    return {HTTP::RequestResult::RequestFailed, 0};
  }

  StreamReaderResult result;
  if (contentLength > 0) {
    result = _readStreamData(client, stream, contentLength, downloadCallback, begin, timeoutMs);
  } else {
    result = _readStreamDataChunked(client, stream, downloadCallback, begin, timeoutMs);
  }

  return {result.result, responseCode, result.nWritten};
}

HTTP::Response<std::size_t>
  HTTP::Download(StringView url, const std::map<String, String>& headers, HTTP::GotContentLengthCallback contentLengthCallback, HTTP::DownloadCallback downloadCallback, const std::vector<int>& acceptedCodes, std::uint32_t timeoutMs) {
  std::shared_ptr<RateLimit> rateLimiter = _getRateLimiter(url);
  if (rateLimiter == nullptr) {
    return {RequestResult::InvalidURL, 0, 0};
  }

  if (!rateLimiter->tryRequest()) {
    return {RequestResult::RateLimited, 0, 0};
  }

  HTTPClient client;
  _setupClient(client);

  return _doGetStream(client, url, headers, acceptedCodes, rateLimiter, contentLengthCallback, downloadCallback, timeoutMs);
}

HTTP::Response<std::string> HTTP::GetString(StringView url, const std::map<String, String>& headers, const std::vector<int>& acceptedCodes, std::uint32_t timeoutMs) {
  std::string result;

  auto allocator = [&result](std::size_t contentLength) {
    result.reserve(contentLength);
    return true;
  };
  auto writer = [&result](std::size_t offset, const uint8_t* data, std::size_t len) {
    result.append(reinterpret_cast<const char*>(data), len);
    return true;
  };

  auto response = Download(url, headers, allocator, writer, acceptedCodes, timeoutMs);
  if (response.result != RequestResult::Success) {
    return {response.result, response.code, {}};
  }

  return {response.result, response.code, result};
}
