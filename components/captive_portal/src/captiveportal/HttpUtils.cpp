#include <freertos/FreeRTOS.h>

#include "captiveportal/HttpUtils.h"

const char* const TAG = "CaptivePortalHttp";

#include "Convert.h"
#include "Logging.h"
#include "StringHelpers.h"
#include "util/HexUtils.h"

using namespace OpenShock;
using namespace OpenShock::CaptivePortal;

esp_err_t CaptivePortal::sendResp(httpd_req_t* req, const char* status, const char* type, std::string_view body)
{
  httpd_resp_set_status(req, status);
  if (type != nullptr) {
    httpd_resp_set_type(req, type);
  }
  return httpd_resp_send(req, body.data(), body.size());
}

void CaptivePortal::registerHandler(httpd_handle_t server, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*), void* userCtx)
{
  httpd_uri_t def = {};
  def.uri         = uri;
  def.method      = method;
  def.handler     = handler;
  def.user_ctx    = userCtx;
  esp_err_t err   = httpd_register_uri_handler(server, &def);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register handler %s: %s", uri, esp_err_to_name(err));
  }
}

// Fails with ParamError::Invalid for an encoded NUL ("%00"), which would silently truncate the value at every c_str() use.
[[nodiscard]] static std::expected<std::string, ParamError> urlDecode(std::string_view s)
{
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
      continue;
    }
    // A '%' without two characters after it is kept as-is.
    if (c == '%' && i + 2 < s.size()) {
      if (auto decoded = HexUtils::TryParseHexPair(s[i + 1], s[i + 2])) {
        if (*decoded == 0) {
          return std::unexpected(ParamError::Invalid);
        }
        out += static_cast<char>(*decoded);
        i += 2;
        continue;
      }
    }
    out += c;
  }
  return out;
}

// The raw (still URL-encoded) value of `key` in "k1=v1&k2=v2", keys compared case-insensitively. A pair without '='
// has an empty value.
[[nodiscard]] static std::optional<std::string_view> findParam(std::string_view params, std::string_view key)
{
  while (!params.empty()) {
    auto [pair, rest]  = StringSplitByFirst(params, '&');
    auto [name, value] = StringSplitByFirst(pair, '=');
    if (StringIEquals(name, key)) {
      return value;
    }
    params = rest;
  }
  return std::nullopt;
}

[[nodiscard]] static std::expected<std::string, ParamError> getParam(std::string_view params, std::string_view key)
{
  auto raw = findParam(params, key);
  if (!raw) {
    return std::unexpected(ParamError::Missing);
  }
  return urlDecode(*raw);
}

std::expected<std::string, ParamError> CaptivePortal::getQueryParam(httpd_req_t* req, std::string_view key)
{
  // req->uri is the request target as received, NUL-terminated and valid for the whole handler
  auto [path, query] = StringSplitByFirst(std::string_view(req->uri), '?');
  return getParam(query, key);
}

std::optional<bool> CaptivePortal::parseBool(std::string_view str)
{
  if (str == "1") {
    return true;
  }
  if (str == "0") {
    return false;
  }
  return Convert::ToBool(str);
}

std::expected<bool, ParamError> CaptivePortal::getBoolQueryParam(httpd_req_t* req, std::string_view key)
{
  auto str = getQueryParam(req, key);
  if (!str) {
    return std::unexpected(str.error());
  }
  auto value = parseBool(*str);
  if (!value) {
    return std::unexpected(ParamError::Invalid);
  }
  return *value;
}

std::optional<std::string> CaptivePortal::recvBody(httpd_req_t* req)
{
  int total = req->content_len;
  if (total <= 0 || total > 4096) {
    return std::nullopt;
  }

  // A client that announces a body and then stalls must not hold the (single) httpd task forever.
  static constexpr int kMaxRecvTimeouts = 3;

  std::string out;
  out.resize(total);
  int off      = 0;
  int timeouts = 0;
  while (off < total) {
    int r = httpd_req_recv(req, out.data() + off, total - off);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) {
      if (++timeouts >= kMaxRecvTimeouts) {
        return std::nullopt;
      }
      continue;
    }
    if (r <= 0) {
      return std::nullopt;
    }
    off += r;
  }
  return out;
}

std::expected<std::string, ParamError> CaptivePortal::getFormParam(std::string_view body, std::string_view key)
{
  return getParam(body, key);
}
