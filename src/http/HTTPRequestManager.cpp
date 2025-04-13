#include "http/HTTPRequestManager.h"

const char* const TAG = "HTTPRequestManager";

#include "Common.h"
#include "Logging.h"
#include "RateLimiter.h"
#include "SimpleMutex.h"
#include "Time.h"
#include "util/HexUtils.h"
#include "util/StringUtils.h"

#include <HTTPClient.h>

#include <algorithm>
#include <memory>
#include <numeric>
#include <string_view>
#include <unordered_map>

using namespace std::string_view_literals;

const std::size_t HTTP_BUFFER_SIZE = 4096LLU;
const int HTTP_DOWNLOAD_SIZE_LIMIT = 200 * 1024 * 1024;  // 200 MB

static OpenShock::SimpleMutex s_rateLimitsMutex                                              = {};
static std::unordered_map<std::string, std::shared_ptr<OpenShock::RateLimiter>> s_rateLimits = {};

using namespace OpenShock;

std::string_view _getDomain(std::string_view url)
{
  if (url.empty()) {
    return {};
  }

  // Remove the protocol eg. "https://api.example.com:443/path" -> "api.example.com:443/path"
  auto seperator = url.find("://");
  if (seperator != std::string_view::npos) {
    url.substr(seperator + 3);
  }

  // Remove the path eg. "api.example.com:443/path" -> "api.example.com:443"
  seperator = url.find('/');
  if (seperator != std::string_view::npos) {
    url = url.substr(0, seperator);
  }

  // Remove the port eg. "api.example.com:443" -> "api.example.com"
  seperator = url.rfind(':');
  if (seperator != std::string_view::npos) {
    url = url.substr(0, seperator);
  }

  // Remove all subdomains eg. "api.example.com" -> "example.com"
  seperator = url.rfind('.');
  if (seperator == std::string_view::npos) {
    return url;  // E.g. "localhost"
  }
  seperator = url.rfind('.', seperator - 1);
  if (seperator != std::string_view::npos) {
    url = url.substr(seperator + 1);
  }

  return url;
}

std::shared_ptr<OpenShock::RateLimiter> _rateLimiterFactory(std::string_view domain)
{
  auto rateLimit = std::make_shared<OpenShock::RateLimiter>();

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

std::shared_ptr<OpenShock::RateLimiter> _getRateLimiter(std::string_view url)
{
  auto domain = std::string(_getDomain(url));
  if (domain.empty()) {
    return nullptr;
  }

  s_rateLimitsMutex.lock(portMAX_DELAY);

  auto it = s_rateLimits.find(domain);
  if (it == s_rateLimits.end()) {
    s_rateLimits.emplace(domain, _rateLimiterFactory(domain));
    it = s_rateLimits.find(domain);
  }

  s_rateLimitsMutex.unlock();

  return it->second;
}

void _setupClient(HTTPClient& client)
{
  client.setUserAgent(OpenShock::Constants::FW_USERAGENT);
}

struct StreamReaderResult {
  HTTP::RequestResult result;
  std::size_t nWritten;
};

constexpr bool _isCRLF(const uint8_t* buffer)
{
  return buffer[0] == '\r' && buffer[1] == '\n';
}
constexpr bool _tryFindCRLF(std::size_t& pos, const uint8_t* buffer, std::size_t len)
{
  const uint8_t* cur = buffer;
  const uint8_t* end = buffer + len - 1;

  while (cur < end) {
    if (_isCRLF(cur)) {
      pos = static_cast<std::size_t>(cur - buffer);
      return true;
    }

    ++cur;
  }

  return false;
}

enum ParserState : uint8_t {
  Ok,
  NeedMoreData,
  Invalid,
};

ParserState _parseChunkHeader(const uint8_t* buffer, std::size_t bufferLen, std::size_t& headerLen, std::size_t& payloadLen)
{
  if (bufferLen < 5) {  // Bare minimum: "0\r\n\r\n"
    return ParserState::NeedMoreData;
  }

  // Find the first CRLF
  if (!_tryFindCRLF(headerLen, buffer, bufferLen)) {
    return ParserState::NeedMoreData;
  }

  // Header must have at least one character
  if (headerLen == 0) {
    OS_LOGW(TAG, "Invalid chunk header length");
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
    OS_LOGW(TAG, "Invalid chunk size field length");
    return ParserState::Invalid;
  }

  std::string_view sizeField(reinterpret_cast<const char*>(buffer), sizeFieldEnd);

  // Parse the chunk size
  if (!HexUtils::TryParseHexToInt(sizeField.data(), sizeField.length(), payloadLen)) {
    OS_LOGW(TAG, "Failed to parse chunk size");
    return ParserState::Invalid;
  }

  if (payloadLen > HTTP_DOWNLOAD_SIZE_LIMIT) {
    OS_LOGW(TAG, "Chunk size too large");
    return ParserState::Invalid;
  }

  // Set the header length to the end of the CRLF
  headerLen += 2;

  return ParserState::Ok;
}

ParserState _parseChunk(const uint8_t* buffer, std::size_t bufferLen, std::size_t& payloadPos, std::size_t& payloadLen)
{
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
    OS_LOGW(TAG, "Invalid chunk payload CRLF");
    return ParserState::Invalid;
  }

  return ParserState::Ok;
}

void _alignChunk(uint8_t* buffer, std::size_t& bufferCursor, std::size_t payloadPos, std::size_t payloadLen)
{
  std::size_t totalLen  = payloadPos + payloadLen + 2;  // +2 for CRLF
  std::size_t remaining = bufferCursor - totalLen;
  if (remaining > 0) {
    memmove(buffer, buffer + totalLen, remaining);
    bufferCursor = remaining;
  } else {
    bufferCursor = 0;
  }
}

StreamReaderResult _readStreamDataChunked(HTTPClient& client, WiFiClient* stream, HTTP::DownloadCallback downloadCallback, int64_t begin, uint32_t timeoutMs)
{
  std::size_t totalWritten   = 0;
  HTTP::RequestResult result = HTTP::RequestResult::Success;

  uint8_t* buffer = static_cast<uint8_t*>(malloc(HTTP_BUFFER_SIZE));
  if (buffer == nullptr) {
    OS_LOGE(TAG, "Out of memory");
    return {HTTP::RequestResult::RequestFailed, 0};
  }

  ParserState state        = ParserState::NeedMoreData;
  std::size_t bufferCursor = 0, payloadPos = 0, payloadSize = 0;

  while (client.connected() && state != ParserState::Invalid) {
    if (begin + timeoutMs < OpenShock::millis()) {
      OS_LOGW(TAG, "Request timed out");
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
      OS_LOGW(TAG, "No bytes read");
      result = HTTP::RequestResult::RequestFailed;
      break;
    }

    bufferCursor += bytesRead;

    while (bufferCursor > 0) {
      state = _parseChunk(buffer, bufferCursor, payloadPos, payloadSize);
      if (state == ParserState::Invalid) {
        OS_LOGE(TAG, "Failed to parse chunk");
        result = HTTP::RequestResult::RequestFailed;
        state  = ParserState::Invalid;  // Mark to exit both loops
        break;
      }
      OS_LOGD(TAG, "Chunk parsed: %zu %zu", payloadPos, payloadSize);

      if (state == ParserState::NeedMoreData) {
        if (bufferCursor == HTTP_BUFFER_SIZE) {
          OS_LOGE(TAG, "Chunk too large");
          result = HTTP::RequestResult::RequestFailed;
          state  = ParserState::Invalid;  // Mark to exit both loops
        }
        break;                            // If chunk size good, this only exits one loop
      }

      // Check for zero chunk size (end of transfer)
      if (payloadSize == 0) {
        state = ParserState::Invalid;  // Mark to exit both loops
        break;
      }

      if (!downloadCallback(totalWritten, buffer + payloadPos, payloadSize)) {
        result = HTTP::RequestResult::Cancelled;
        state  = ParserState::Invalid;  // Mark to exit both loops
        break;
      }

      totalWritten += payloadSize;
      _alignChunk(buffer, bufferCursor, payloadPos, payloadSize);
      payloadSize = 0;
      payloadPos  = 0;
    }

    if (state == ParserState::NeedMoreData) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  free(buffer);

  return {result, totalWritten};
}

StreamReaderResult _readStreamData(HTTPClient& client, WiFiClient* stream, std::size_t contentLength, HTTP::DownloadCallback downloadCallback, int64_t begin, uint32_t timeoutMs)
{
  std::size_t nWritten       = 0;
  HTTP::RequestResult result = HTTP::RequestResult::Success;

  uint8_t* buffer = static_cast<uint8_t*>(malloc(HTTP_BUFFER_SIZE));

  while (client.connected() && nWritten < contentLength) {
    if (begin + timeoutMs < OpenShock::millis()) {
      OS_LOGW(TAG, "Request timed out");
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
      OS_LOGW(TAG, "No bytes read");
      result = HTTP::RequestResult::RequestFailed;
      break;
    }

    if (!downloadCallback(nWritten, buffer, bytesRead)) {
      OS_LOGW(TAG, "Request cancelled by callback");
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
  std::string_view url,
  const std::map<String, String>& headers,
  tcb::span<const uint16_t> acceptedCodes,
  std::shared_ptr<OpenShock::RateLimiter> rateLimiter,
  HTTP::GotContentLengthCallback contentLengthCallback,
  HTTP::DownloadCallback downloadCallback,
  uint32_t timeoutMs
)
{
  int64_t begin = OpenShock::millis();
  if (!client.begin(OpenShock::StringToArduinoString(url))) {
    OS_LOGE(TAG, "Failed to begin HTTP request");
    return {HTTP::RequestResult::RequestFailed, 0, 0};
  }

  for (auto& header : headers) {
    client.addHeader(header.first, header.second);
  }

  int responseCode = client.GET();

  if (responseCode == HTTP_CODE_REQUEST_TIMEOUT || begin + timeoutMs < OpenShock::millis()) {
    OS_LOGW(TAG, "Request timed out");
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

    // Apply the block-for time
    rateLimiter->blockFor(retryAfter * 1000);

    return {HTTP::RequestResult::RateLimited, responseCode, 0};
  }

  if (responseCode == 418) {
    OS_LOGW(TAG, "The server refused to brew coffee because it is, permanently, a teapot.");
  }

  if (std::find(acceptedCodes.begin(), acceptedCodes.end(), responseCode) == acceptedCodes.end()) {
    OS_LOGD(TAG, "Received unexpected response code %d", responseCode);
    return {HTTP::RequestResult::CodeRejected, responseCode, 0};
  }

  int contentLength = client.getSize();
  if (contentLength == 0) {
    return {HTTP::RequestResult::Success, responseCode, 0};
  }

  if (contentLength > 0) {
    if (contentLength > HTTP_DOWNLOAD_SIZE_LIMIT) {
      OS_LOGE(TAG, "Content-Length too large");
      return {HTTP::RequestResult::RequestFailed, responseCode, 0};
    }

    if (!contentLengthCallback(contentLength)) {
      OS_LOGW(TAG, "Request cancelled by callback");
      return {HTTP::RequestResult::Cancelled, responseCode, 0};
    }
  }

  WiFiClient* stream = client.getStreamPtr();
  if (stream == nullptr) {
    OS_LOGE(TAG, "Failed to get stream");
    return {HTTP::RequestResult::RequestFailed, 0, 0};
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
  HTTP::Download(std::string_view url, const std::map<String, String>& headers, HTTP::GotContentLengthCallback contentLengthCallback, HTTP::DownloadCallback downloadCallback, tcb::span<const uint16_t> acceptedCodes, uint32_t timeoutMs)
{
  std::shared_ptr<OpenShock::RateLimiter> rateLimiter = _getRateLimiter(url);
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

HTTP::Response<std::string> HTTP::GetString(std::string_view url, const std::map<String, String>& headers, tcb::span<const uint16_t> acceptedCodes, uint32_t timeoutMs)
{
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
