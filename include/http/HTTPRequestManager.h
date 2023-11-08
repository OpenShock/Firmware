#pragma once

#include "util/JsonRoot.h"

#include <Arduino.h>

#include <map>
#include <vector>
#include <functional>

namespace OpenShock::HTTPRequestManager {
  enum class RequestResult : std::uint8_t {
    RequestFailed,
    ServerError,
    RateLimited,
    ResponseCodeNotOK,
    ParseFailed,
    Success
  };

  struct Request {
    const char* const url;
    std::map<String, String> headers;
    std::vector<int> okCodes;
    bool blockOnRateLimit;
  };

  template <typename T>
  struct Response {
    RequestResult result;
    int code;
    T data;
  };

  bool Init();

  Response<String> GetString(const Request& request);

  template <typename T>
  Response<T> GetJSON(const Request& request, std::function<bool(int code, const cJSON* json, T& data)> jsonParser) {
    Response<String> response = GetString(request);
    if (
      response.result == RequestResult::RequestFailed     ||
      response.result == RequestResult::RateLimited       ||
      response.result == RequestResult::ResponseCodeNotOK
      ) {
      return { response.result, response.code, {} };
    }

    OpenShock::JsonRoot json = OpenShock::JsonRoot::Parse(response.data);
    if (!json.isValid()) {
      return { RequestResult::ParseFailed, response.code, {} };
    }

    T data;
    if (!jsonParser(response.code, json, data)) {
      return { RequestResult::ParseFailed, response.code, {} };
    }

    return { response.result, response.code, data };
  }
}  // namespace OpenShock::HTTPRequestManager
