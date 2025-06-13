#pragma once

#include <Arduino.h>

#include <cJSON.h>

#include <functional>
#include <map>
#include <string_view>

#include "span.h"

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
  using JsonParser               = std::function<bool(int code, const cJSON* json, T& data)>;
  using GotContentLengthCallback = std::function<bool(int contentLength)>;
  using DownloadCallback         = std::function<bool(std::size_t offset, const uint8_t* data, std::size_t len)>;

  Response<std::size_t> Download(std::string_view url, const std::map<String, String>& headers, GotContentLengthCallback contentLengthCallback, DownloadCallback downloadCallback, tcb::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000);
  Response<std::string> GetString(std::string_view url, const std::map<String, String>& headers, tcb::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000);

  template<typename T>
  Response<T> GetJSON(std::string_view url, const std::map<String, String>& headers, JsonParser<T> jsonParser, tcb::span<const uint16_t> acceptedCodes, uint32_t timeoutMs = 10'000) {
    auto response = GetString(url, headers, acceptedCodes, timeoutMs);
    if (response.result != RequestResult::Success) {
      return {response.result, response.code, {}};
    }

    cJSON* json = cJSON_ParseWithLength(response.data.c_str(), response.data.length());
    if (json == nullptr) {
      return {RequestResult::ParseFailed, response.code, {}};
    }

    T data;
    if (!jsonParser(response.code, json, data)) {
      return {RequestResult::ParseFailed, response.code, {}};
    }

    cJSON_Delete(json);

    return {response.result, response.code, std::move(data)};
  }
}  // namespace OpenShock::HTTP
