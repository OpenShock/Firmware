#pragma once

#include <esp_http_server.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>

// Small helpers over the esp_http_server request API, for the portal's handlers.
namespace OpenShock::CaptivePortal {
  // HTTP status lines (esp_http_server needs the full "code reason" string, and stores
  // the pointer rather than copying, so these must have static storage duration).
  inline constexpr const char* S200 = "200 OK";
  inline constexpr const char* S304 = "304 Not Modified";
  inline constexpr const char* S400 = "400 Bad Request";
  inline constexpr const char* S429 = "429 Too Many Requests";
  inline constexpr const char* S500 = "500 Internal Server Error";
  inline constexpr const char* S503 = "503 Service Unavailable";

  enum class ParamError {
    Missing,
    Invalid,  // present, but not decodable (an encoded NUL) or not a valid value
  };

  esp_err_t sendResp(httpd_req_t* req, const char* status, const char* type, std::string_view body);

  /// @brief Registers a route, logging a failure. With wildcard matching, routes registered earlier take priority.
  void registerHandler(httpd_handle_t server, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*), void* userCtx);

  // Read and URL-decode a query-string parameter. httpd does not decode for us.
  [[nodiscard]] std::expected<std::string, ParamError> getQueryParam(httpd_req_t* req, const char* key);
  [[nodiscard]] std::expected<bool, ParamError> getBoolQueryParam(httpd_req_t* req, const char* key);

  // The web UI sends booleans as "1"/"0"; "true"/"false" are accepted too.
  [[nodiscard]] std::optional<bool> parseBool(std::string_view str);

  // Read a urlencoded request body (application/x-www-form-urlencoded).
  [[nodiscard]] std::optional<std::string> recvBody(httpd_req_t* req);
  [[nodiscard]] std::expected<std::string, ParamError> getFormParam(const std::string& body, const char* key);
}  // namespace OpenShock::CaptivePortal
