#include <freertos/FreeRTOS.h>

#include "captiveportal/HttpUtils.h"

const char* const TAG = "CaptivePortalHttp";

#include "Convert.h"
#include "Logging.h"
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

std::expected<std::string, ParamError> CaptivePortal::getQueryParam(httpd_req_t* req, const char* key)
{
  size_t qlen = httpd_req_get_url_query_len(req);
  if (qlen == 0) {
    return std::unexpected(ParamError::Missing);
  }

  std::string query;
  query.resize(qlen + 1);
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
    return std::unexpected(ParamError::Missing);
  }

  char val[256];
  if (httpd_query_key_value(query.c_str(), key, val, sizeof(val)) != ESP_OK) {
    return std::unexpected(ParamError::Missing);
  }

  return urlDecode(val);
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

std::expected<bool, ParamError> CaptivePortal::getBoolQueryParam(httpd_req_t* req, const char* key)
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

std::expected<std::string, ParamError> CaptivePortal::getFormParam(const std::string& body, const char* key)
{
  char val[256];
  if (httpd_query_key_value(body.c_str(), key, val, sizeof(val)) != ESP_OK) {
    return std::unexpected(ParamError::Missing);
  }
  return urlDecode(val);
}
