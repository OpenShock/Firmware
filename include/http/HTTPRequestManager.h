#pragma once

#include "util/JsonRoot.h"

#include <Arduino.h>

#include <functional>
#include <map>
#include <vector>

namespace OpenShock::HTTP {
  enum class RequestResult : std::uint8_t {
    InvalidURL,     // Invalid URL
    RequestFailed,  // Failed to start request
    RateLimited,    // Rate limited (can be both local and global)
    CodeRejected,   // Request completed, but response code was not OK
    ParseFailed,    // Request completed, but JSON parsing failed
    Success,        // Request completed successfully
  };

  template<typename T>
  struct Response {
    RequestResult result;
    int code;
    T data;
  };

  template<typename T>
  using JsonParser = std::function<bool(int code, const cJSON* json, T& data)>;

  Response<String> GetString(const char* const url, const std::map<String, String>& headers, std::vector<int> acceptedCodes = {200});

  template<typename T>
  Response<T> GetJSON(const char* const url, const std::map<String, String>& headers, JsonParser<T> jsonParser, std::vector<int> acceptedCodes = {200}) {
    auto response = GetString(url, headers, acceptedCodes);
    if (response.result != RequestResult::Success) {
      return {response.result, response.code, {}};
    }

    OpenShock::JsonRoot json = OpenShock::JsonRoot::Parse(response.data);
    if (!json.isValid()) {
      return {RequestResult::ParseFailed, response.code, {}};
    }

    T data;
    if (!jsonParser(response.code, json, data)) {
      return {RequestResult::ParseFailed, response.code, {}};
    }

    return {response.result, response.code, data};
  }
}  // namespace OpenShock::HTTP
