#pragma once

#include "json/Json.h"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Forward declarations so this public header doesn't pull in <esp_http_client.h>.
// These match the real typedefs exactly, so including the real header elsewhere
// in the same translation unit is fine.
typedef int esp_err_t;
typedef struct esp_http_client* esp_http_client_handle_t;
typedef struct esp_http_client_event esp_http_client_event_t;

namespace OpenShock::HTTP {
  enum class RequestResult : uint8_t {
    InternalError,  // Internal error
    InvalidURL,     // Invalid URL
    RequestFailed,  // Failed to start request
    TimedOut,       // Request timed out
    RateLimited,    // Rate limited (can be both local and global)
    CodeRejected,   // Request completed, but response code was not OK
    ParseFailed,    // Request completed, but JSON parsing failed
    Cancelled,      // Request was cancelled
    Success,        // Request completed successfully
  };

  template<typename T>
  struct [[nodiscard]] Response {
    RequestResult result;
    int code;
    T data;

    Response(RequestResult r, int c, T d)
      : result(r)
      , code(c)
      , data(std::move(d))
    {
    }

    inline const char* ResultToString() const
    {
      switch (result) {
        case RequestResult::InternalError:
          return "Internal error";
        case RequestResult::InvalidURL:
          return "Requested url was invalid";
        case RequestResult::RequestFailed:
          return "Request failed";
        case RequestResult::TimedOut:
          return "Request timed out";
        case RequestResult::RateLimited:
          return "Client was ratelimited";
        case RequestResult::CodeRejected:
          return "Unexpected response code";
        case RequestResult::ParseFailed:
          return "Parsing the response failed";
        case RequestResult::Cancelled:
          return "Request was cancelled";
        case RequestResult::Success:
          return "Success";
        default:
          return "Unknown reason";
      }
    }
  };

  template<typename T>
  using JsonParser               = std::function<bool(int code, JSON::JsonView json, T& data)>;
  using GotContentLengthCallback = std::function<bool(int contentLength)>;
  using DownloadCallback         = std::function<bool(std::size_t offset, const uint8_t* data, std::size_t len)>;

  // A reusable HTTP client. Sequential requests on the same instance reuse the
  // underlying TCP/TLS connection (HTTP keep-alive) when they target the same
  // host, and transparently reconnect when the host changes or the connection
  // drops. The caller owns the lifetime: construct one where several requests
  // are issued back-to-back (e.g. an OTA update) so they share a connection.
  //
  // Not thread-safe. Use a separate Client per task.
  class Client {
  public:
    Client() noexcept;
    ~Client();

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&)                 = delete;
    Client& operator=(Client&&)      = delete;

    Response<std::size_t> Download(std::string_view url, const std::map<std::string, std::string>& headers, GotContentLengthCallback contentLengthCallback, DownloadCallback downloadCallback, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000);
    Response<std::string> GetString(std::string_view url, const std::map<std::string, std::string>& headers, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000);

    template<typename T>
    Response<T> GetJSON(std::string_view url, const std::map<std::string, std::string>& headers, JsonParser<T> jsonParser, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000)
    {
      auto response = GetString(url, headers, acceptedCodes, timeoutMs);
      if (response.result != RequestResult::Success) {
        return {response.result, response.code, {}};
      }

      // The parsed views point into response.data, which outlives this call; the
      // parser copies out everything it needs before we return.
      JSON::JsonDocument doc;
      if (!doc.parse(response.data)) {
        return {RequestResult::ParseFailed, response.code, {}};
      }

      T data;
      if (!jsonParser(response.code, doc.root(), data)) {
        return {RequestResult::ParseFailed, response.code, {}};
      }

      return {response.result, response.code, std::move(data)};
    }

  private:
    static esp_err_t eventHandler(esp_http_client_event_t* evt);
    void drop();

    esp_http_client_handle_t m_handle;
    std::vector<std::string> m_headerKeys;  // headers set on the last request, cleared before reuse
    std::string m_retryAfter;               // "Retry-After" response header, captured by eventHandler
    bool m_connectionClose;                 // server sent "Connection: close"
  };

  // One-shot helpers: perform a single request on a temporary client. Use a
  // Client directly when issuing several requests to the same host.
  inline Response<std::size_t> Download(std::string_view url, const std::map<std::string, std::string>& headers, GotContentLengthCallback contentLengthCallback, DownloadCallback downloadCallback, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000)
  {
    Client client;
    return client.Download(url, headers, std::move(contentLengthCallback), std::move(downloadCallback), acceptedCodes, timeoutMs);
  }

  inline Response<std::string> GetString(std::string_view url, const std::map<std::string, std::string>& headers, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000)
  {
    Client client;
    return client.GetString(url, headers, acceptedCodes, timeoutMs);
  }

  template<typename T>
  Response<T> GetJSON(std::string_view url, const std::map<std::string, std::string>& headers, JsonParser<T> jsonParser, std::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000)
  {
    Client client;
    return client.GetJSON<T>(url, headers, std::move(jsonParser), acceptedCodes, timeoutMs);
  }
}  // namespace OpenShock::HTTP
