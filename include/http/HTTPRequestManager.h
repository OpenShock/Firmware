#pragma once

#include <Arduino.h>

#include <cJSON.h>

#include <functional>
#include <map>
#include <vector>

namespace OpenShock::HTTP {
  enum class RequestResult : std::uint8_t {
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
  struct Response {
    RequestResult result;
    int code;
    T data;
  };

  template<typename T>
  using JsonParser               = std::function<bool(int code, const cJSON* json, T& data)>;
  using GotContentLengthCallback = std::function<bool(int contentLength)>;
  using DownloadCallback         = std::function<bool(std::size_t offset, const uint8_t* data, std::size_t len)>;

  Response<std::size_t> Download(const char* const url, const std::map<String, String>& headers, GotContentLengthCallback contentLengthCallback, DownloadCallback downloadCallback, const std::vector<int>& acceptedCodes = {200}, std::uint32_t timeoutMs = 10'000);
  Response<std::string> GetString(const char* const url, const std::map<String, String>& headers, const std::vector<int>& acceptedCodes = {200}, std::uint32_t timeoutMs = 10'000);

  template<typename T>
  Response<T> GetJSON(const char* const url, const std::map<String, String>& headers, JsonParser<T> jsonParser, const std::vector<int>& acceptedCodes = {200}, std::uint32_t timeoutMs = 10'000) {
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

    return {response.result, response.code, data};
  }
}  // namespace OpenShock::HTTP
