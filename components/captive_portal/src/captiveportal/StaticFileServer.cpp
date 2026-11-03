#include <freertos/FreeRTOS.h>

#include "captiveportal/StaticFileServer.h"

const char* const TAG = "CaptivePortalStatic";

#include "captiveportal/HttpUtils.h"
#include "http/ContentTypes.h"
#include "Logging.h"
#include "rfc8908/RFC8908Handler.h"
#include "StringHelpers.h"
#include "util/HexUtils.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using namespace OpenShock;

static const char* contentTypeForPath(std::string_view path)
{
  // Walk back to the dot. The longest known extension is 5 characters, so give up after that many, or at a '/'.
  static constexpr std::size_t kMaxExtensionLen = 5;
  std::size_t i                                 = path.size();
  while (true) {
    if (i == 0 || path.size() - i > kMaxExtensionLen) {
      return HTTP::ContentType::OctetStream;
    }
    char c = path[--i];
    if (c == '.') break;
    if (c == '/') {
      return HTTP::ContentType::OctetStream;
    }
  }
  std::string_view ext = path.substr(i + 1);

  static constexpr struct {
    std::string_view extension;
    const char* contentType;
  } kContentTypes[] = {
    {   "js", HTTP::ContentType::JavaScript},
    {  "css",        HTTP::ContentType::CSS},
    { "html",   HTTP::ContentType::TextHTML},
    {"woff2",      HTTP::ContentType::WOFF2},
    {  "svg",        HTTP::ContentType::SVG},
    {  "png",        HTTP::ContentType::PNG},
    {  "ico",       HTTP::ContentType::Icon},
    { "json",       HTTP::ContentType::JSON},
    {  "txt",  HTTP::ContentType::TextPlain},
  };

  for (const auto& entry : kContentTypes) {
    if (StringIEquals(entry.extension, ext)) {
      return entry.contentType;
    }
  }
  return HTTP::ContentType::OctetStream;
}

// RFC 9110 §12.5.3. An explicit gzip entry decides; otherwise a wildcard does; otherwise gzip is not acceptable.
// A weight of 0 ("gzip;q=0") excludes the coding.
static bool acceptsGzip(std::string_view acceptEncoding)
{
  // weight = OWS ";" OWS "q=" qvalue, where qvalue is zero for "0" optionally followed by "." and up to three "0"s
  auto weightIsZero = [](std::string_view params) {
    params = StringTrim(params);
    if (params.size() < 3 || CharToLower(params[0]) != 'q' || params[1] != '=' || params[2] != '0') {
      return false;  // no weight (defaults to 1), or a nonzero one
    }
    for (char c : params.substr(3)) {
      if (c != '.' && c != '0') return false;
    }
    return true;
  };

  bool wildcard = false;
  while (!acceptEncoding.empty()) {
    std::size_t comma     = acceptEncoding.find(',');
    std::string_view item = acceptEncoding.substr(0, comma);
    acceptEncoding.remove_prefix(comma == std::string_view::npos ? acceptEncoding.size() : comma + 1);

    std::size_t semicolon   = item.find(';');
    std::string_view coding = StringTrim(item.substr(0, semicolon));
    std::string_view params = semicolon == std::string_view::npos ? std::string_view() : item.substr(semicolon + 1);

    if (StringIEquals(coding, "gzip") || StringIEquals(coding, "x-gzip")) {  // x-gzip is an alias (RFC 9110 §8.4.1.3)
      return !weightIsZero(params);
    }
    if (coding == "*") {
      wildcard = !weightIsZero(params);
    }
  }
  return wildcard;
}

// Without an Accept-Encoding header any coding may be sent; an empty one means identity only (RFC 9110 §12.5.3).
// The length lookup can't tell the two apart, but the value lookup can.
static bool clientAcceptsGzip(httpd_req_t* req)
{
  // Browsers send at most "gzip, deflate, br, zstd" (23 bytes), which fits; a longer value goes to the heap
  char value[32];
  esp_err_t err = httpd_req_get_hdr_value_str(req, "Accept-Encoding", value, sizeof(value));
  if (err == ESP_ERR_NOT_FOUND) {
    return true;
  }
  if (err == ESP_OK) {
    return acceptsGzip(value);
  }
  if (err != ESP_ERR_HTTPD_RESULT_TRUNC) {
    return false;
  }

  std::size_t len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
  std::string longValue(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", longValue.data(), longValue.size()) != ESP_OK) {
    return false;
  }
  return acceptsGzip(std::string_view(longValue.data(), len));
}

static constexpr const char* kCacheControl          = "max-age=3600";
static constexpr const char* kCacheControlImmutable = "public, max-age=31536000, immutable";  // content-addressed name

// What a static response says about itself, shared by the full response, the 304 and the HEAD response.
struct Representation {
  const char* contentType;
  const char* cacheControl;
  const char* etag;  // quoted
  std::size_t size;
};

static bool matchesIfNoneMatch(httpd_req_t* req, const char* etag)
{
  char inm[80];
  return httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && std::strcmp(etag, inm) == 0;
}

// Validator and cache headers go on the 304 too, so a cache can refresh its stored response (RFC 9110 §15.4.5)
static void setHeaders(httpd_req_t* req, const Representation& rep)
{
  httpd_resp_set_type(req, rep.contentType);
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  httpd_resp_set_hdr(req, "Cache-Control", rep.cacheControl);
  httpd_resp_set_hdr(req, "ETag", rep.etag);
}

static esp_err_t sendNotModified(httpd_req_t* req, const Representation& rep)
{
  setHeaders(req, rep);
  httpd_resp_set_status(req, CaptivePortal::S304);
  return httpd_resp_send(req, nullptr, 0);
}

// A gzip-only asset for a client that excluded gzip: there is no inflater on the device, so no acceptable
// representation exists (RFC 9110 §12.5.3). A redirect here would send the client back to the same URL.
static esp_err_t sendNotAcceptable(httpd_req_t* req)
{
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  return CaptivePortal::sendResp(req, CaptivePortal::S406, HTTP::ContentType::TextPlain, "Only available with Content-Encoding: gzip\n");
}

// httpd derives Content-Length from the buffer it is handed and has no bodiless send, so the HEAD response (the GET's
// headers with the file's size, RFC 9110 §9.3.2) is written out directly.
static esp_err_t sendHeadResponse(httpd_req_t* req, const Representation& rep)
{
  // Every field is a short literal or the 66-byte ETag, so the head always fits
  char head[320];
  int len = snprintf(
    head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nContent-Encoding: gzip\r\nVary: Accept-Encoding\r\nCache-Control: %s\r\nETag: %s\r\n\r\n", CaptivePortal::S200, rep.contentType, rep.size, rep.cacheControl, rep.etag
  );
  if (len < 0 || static_cast<std::size_t>(len) >= sizeof(head)) {
    return ESP_FAIL;
  }

  std::size_t sent = 0;
  while (sent < static_cast<std::size_t>(len)) {
    int n = httpd_send(req, head + sent, static_cast<std::size_t>(len) - sent);
    if (n <= 0) {
      return ESP_FAIL;
    }
    sent += static_cast<std::size_t>(n);
  }
  return ESP_OK;
}

CaptivePortal::StaticFileServer::StaticFileServer()
{
  if (!m_www.mount(Www::FindPartition())) {
    OS_LOGE(TAG, "The portal has no assets until a www image is flashed, serving the error page");  // mount() logged why
    return;
  }
  if (!m_www.find("index.html")) {
    OS_LOGE(TAG, "The www image has no index.html, serving the error page");
    m_www.unmount();
    return;
  }
  char hex[Www::kBodyHashSize * 2 + 1];
  HexUtils::ToHex(m_www.image().BodyHash(), hex, false);
  snprintf(m_etag, sizeof(m_etag), "\"%s\"", hex);
  OS_LOGI(TAG, "Serving files from the www image (%s)", m_etag);
}

void CaptivePortal::StaticFileServer::registerHandler(httpd_handle_t server)
{
  // Decided once: the assets are mounted in the constructor and stay that way until destruction
  if (m_www.isMounted()) {
    CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::staticFileHandler, this);
    CaptivePortal::registerHandler(server, "/*", HTTP_HEAD, &StaticFileServer::staticFileHandler, this);
  } else {
    CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::missingAssetsHandler, nullptr);
  }
}

esp_err_t CaptivePortal::StaticFileServer::missingAssetsHandler(httpd_req_t* req)
{
  // No usable assets on the device; serve the help page for any request.
  return sendResp(
    req,
    S200,
    HTTP::ContentType::TextPlain,
    R"(This hub has no captive portal assets.
They are installed with every firmware update; flashing the merged image for your board over USB also restores them.
If this happened with a file we provided or you just need help, come to the Discord!

discord.gg/OpenShock
)"
  );
}

esp_err_t CaptivePortal::StaticFileServer::staticFileHandler(httpd_req_t* req)
{
  auto* self = static_cast<StaticFileServer*>(req->user_ctx);

  // Path only: httpd leaves the query string in req->uri
  std::string_view uri(req->uri);
  uri = uri.substr(0, uri.find('?'));

  // A ".." segment is never a valid asset path. Browsers normalize these away before
  // sending, so this only comes from hand-built requests: refuse it rather than answer like a captive probe.
  if (uri.find("..") != std::string_view::npos) {
    return sendResp(req, S400, HTTP::ContentType::TextPlain, "Bad Request\n");
  }

  if (uri == "/") {
    uri = "/index.html";
  }

  // Stored without the leading slash. Every body in the image is gzipped.
  auto asset = self->m_www.find(uri.substr(1));
  if (!asset) {
    // Unknown path -> captive redirect (the index fallback of a static server).
    return RFC8908::EmitRedirect(req);
  }
  if (!clientAcceptsGzip(req)) {
    return sendNotAcceptable(req);
  }

  Representation rep {
    .contentType  = contentTypeForPath(uri),
    .cacheControl = asset->immutable() ? kCacheControlImmutable : kCacheControl,
    .etag         = self->m_etag,
    .size         = asset->body.size(),
  };

  if (matchesIfNoneMatch(req, rep.etag)) {
    return sendNotModified(req, rep);
  }
  if (req->method == HTTP_HEAD) {
    return sendHeadResponse(req, rep);
  }

  setHeaders(req, rep);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");  // every body in the image is gzipped
  // Straight from the flash mapping: no copy, no filesystem
  return httpd_resp_send(req, reinterpret_cast<const char*>(asset->body.data()), static_cast<ssize_t>(asset->body.size()));
}
