#include <freertos/FreeRTOS.h>

#include "captiveportal/StaticFileServer.h"

const char* const TAG = "CaptivePortalStatic";

#include "captiveportal/HttpUtils.h"
#include "http/ContentTypes.h"
#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "rfc8908/RFC8908Handler.h"
#include "SimpleMutex.h"
#include "StringHelpers.h"
#include "util/HexUtils.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

using namespace OpenShock;

namespace {
  // Hashing a whole partition takes a noticeable time and its content only changes through OTA, which restarts the
  // device, so each partition is hashed once per boot. Entries are only ever appended, under the mutex, so a pointer
  // into the table stays valid.
  struct PartitionHash {
    const esp_partition_t* partition;
    char hash[65];
  };
  constexpr std::size_t kMaxHashedPartitions = 2;
  PartitionHash s_partitionHashes[kMaxHashedPartitions];
  std::size_t s_partitionHashCount = 0;
  SimpleMutex s_partitionHashesMutex;

  // Caller holds s_partitionHashesMutex
  const char* findPartitionHash(const esp_partition_t* partition)
  {
    for (std::size_t i = 0; i < s_partitionHashCount; ++i) {
      if (s_partitionHashes[i].partition == partition) {
        return s_partitionHashes[i].hash;
      }
    }
    return nullptr;
  }
}  // namespace

/// @brief The partition's SHA-256 as hex, computed on first use. Null for a null partition, if hashing failed, or if
///        the table is full.
static const char* getPartitionHash(const esp_partition_t* partition)
{
  if (partition == nullptr) {
    return nullptr;
  }
  {
    ScopedLock lock__(&s_partitionHashesMutex);
    if (const char* hash = findPartitionHash(partition)) {
      return hash;
    }
  }

  // Hash with the mutex released: a retired instance's httpd task may be looking up its own tag meanwhile
  char hash[65];
  if (!TryGetPartitionHash(partition, hash)) {
    return nullptr;
  }

  ScopedLock lock__(&s_partitionHashesMutex);
  if (const char* existing = findPartitionHash(partition)) {
    return existing;  // Another task hashed the same partition meanwhile
  }
  if (s_partitionHashCount == kMaxHashedPartitions) {
    OS_LOGW(TAG, "Partition hash table full, not caching");
    return nullptr;
  }
  PartitionHash& entry = s_partitionHashes[s_partitionHashCount++];
  entry.partition      = partition;
  std::memcpy(entry.hash, hash, sizeof(hash));
  return entry.hash;
}

static const char* contentTypeForPath(std::string_view path)
{
  // Assets are stored pre-compressed as <name>.gz: the type is that of the content inside, not of the wrapper
  if (path.size() >= 3 && path[path.size() - 3] == '.' && CharToLower(path[path.size() - 2]) == 'g' && CharToLower(path[path.size() - 1]) == 'z') {
    path.remove_suffix(3);
  }

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
    if (entry.extension.size() != ext.size()) continue;
    std::size_t j = 0;
    while (j < ext.size() && CharToLower(ext[j]) == entry.extension[j]) ++j;
    if (j == ext.size()) {
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
  std::size_t len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
  std::string value;
  value.resize(len + 1);
  esp_err_t err = httpd_req_get_hdr_value_str(req, "Accept-Encoding", value.data(), value.size());
  if (err == ESP_ERR_NOT_FOUND) {
    return true;
  }
  if (err != ESP_OK) {
    return false;
  }
  return acceptsGzip(std::string_view(value.data(), len));
}

static constexpr const char* kCacheControl          = "max-age=3600";
static constexpr const char* kCacheControlImmutable = "public, max-age=31536000, immutable";  // content-addressed name

// What a static response says about itself, shared by the full response, the 304 and the HEAD response.
struct Representation {
  const char* contentType;
  bool gzip;
  const char* cacheControl;
  const char* etag;  // quoted, or null when unknown
  std::size_t size;
};

static bool matchesIfNoneMatch(httpd_req_t* req, const char* etag)
{
  if (etag == nullptr) {
    return false;
  }
  char inm[80];
  return httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && std::strcmp(etag, inm) == 0;
}

// Validator and cache headers go on the 304 too, so a cache can refresh its stored response (RFC 9110 §15.4.5)
static void setHeaders(httpd_req_t* req, const Representation& rep)
{
  httpd_resp_set_type(req, rep.contentType);
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  httpd_resp_set_hdr(req, "Cache-Control", rep.cacheControl);
  if (rep.etag != nullptr) {
    httpd_resp_set_hdr(req, "ETag", rep.etag);
  }
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
  std::string head;
  head.reserve(256);
  head += "HTTP/1.1 ";
  head += CaptivePortal::S200;
  head += "\r\nContent-Type: ";
  head += rep.contentType;
  head += "\r\nContent-Length: ";
  head += std::to_string(rep.size);
  if (rep.gzip) {
    head += "\r\nContent-Encoding: gzip";
  }
  head += "\r\nVary: Accept-Encoding\r\nCache-Control: ";
  head += rep.cacheControl;
  if (rep.etag != nullptr) {
    head += "\r\nETag: ";
    head += rep.etag;
  }
  head += "\r\n\r\n";

  std::size_t sent = 0;
  while (sent < head.size()) {
    int n = httpd_send(req, head.data() + sent, head.size() - sent);
    if (n <= 0) {
      return ESP_FAIL;
    }
    sent += static_cast<std::size_t>(n);
  }
  return ESP_OK;
}

CaptivePortal::StaticFileServer::StaticFileServer()
{
  // The portal assets: the www image where the partition table has one, else (1.x layout) littlefs static0.
  if (const esp_partition_t* www = Www::FindPartition(); www != nullptr) {
    if (!m_www.mount(www)) {
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
    m_etag = std::string("\"") + hex + "\"";
    OS_LOGI(TAG, "Serving files from the www image (%s)", m_etag.c_str());
    return;
  }

  // Mount the static filesystem (gzipped portal assets) read-only via raw littlefs.
  const esp_partition_t* partition = FindStaticPartition();
  if (partition == nullptr) {
    OS_LOGE(TAG, "Failed to find a www or static filesystem partition");
  } else if (!m_partition.mount(partition, false)) {
    OS_LOGE(TAG, "Failed to mount static filesystem");
  } else if (!m_partition.fileSize("/www/index.html.gz") && !m_partition.fileSize("/www/index.html")) {
    OS_LOGE(TAG, "/www/index.html(.gz) not found, serving error page");
    m_partition.unmount();
  } else {
    // Hash now, so the first request doesn't pay for it
    const char* hash = getPartitionHash(partition);
    OS_LOGI(TAG, "Serving files from littlefs (hash: %s)", hash != nullptr ? hash : "?");
  }
}

void CaptivePortal::StaticFileServer::registerHandler(httpd_handle_t server)
{
  // Decided once: the assets are mounted in the constructor and stay that way until destruction
  if (m_www.isMounted() || m_partition.isMounted()) {
    CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::staticFileHandler, this);
    CaptivePortal::registerHandler(server, "/*", HTTP_HEAD, &StaticFileServer::staticFileHandler, this);
  } else {
    CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::missingFsHandler, nullptr);
  }
}

esp_err_t CaptivePortal::StaticFileServer::missingFsHandler(httpd_req_t* req)
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

  // A ".." segment is never a valid asset path (littlefs would resolve it). Browsers normalize these away before
  // sending, so this only comes from hand-built requests: refuse it rather than answer like a captive probe.
  if (uri.find("..") != std::string_view::npos) {
    return sendResp(req, S400, HTTP::ContentType::TextPlain, "Bad Request\n");
  }

  if (uri == "/") {
    uri = "/index.html";
  }

  bool wantsGzip = clientAcceptsGzip(req);

  return self->m_www.isMounted() ? self->serveFromImage(req, uri, wantsGzip) : self->serveFromLittlefs(req, uri, wantsGzip);
}

esp_err_t CaptivePortal::StaticFileServer::serveFromImage(httpd_req_t* req, std::string_view uri, bool wantsGzip)
{
  // Stored without the leading slash. Every body in the image is gzipped.
  auto asset = m_www.find(uri.substr(1));
  if (!asset) {
    // Unknown path -> captive redirect (the index fallback of a static server).
    return RFC8908::EmitRedirect(req);
  }
  if (!wantsGzip) {
    return sendNotAcceptable(req);
  }

  Representation rep {
    .contentType  = contentTypeForPath(uri),
    .gzip         = true,
    .cacheControl = asset->immutable() ? kCacheControlImmutable : kCacheControl,
    .etag         = m_etag.empty() ? nullptr : m_etag.c_str(),
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

esp_err_t CaptivePortal::StaticFileServer::serveFromLittlefs(httpd_req_t* req, std::string_view uri, bool wantsGzip)
{
  // Assets live at /www/<path>, usually pre-compressed as /www/<path>.gz. A client that accepts gzip is offered the
  // compressed file first; anyone else, or a path with no .gz, gets the plain file if the image has one.
  std::string path;
  path.reserve(4 + uri.size() + 3);
  path += "/www";
  path += uri;

  // fileSize only answers for regular files: littlefs collapses "//" and "." segments, so a path can land on a
  // directory, which must not be opened as a file.
  std::optional<std::size_t> size;
  bool gzip = false;
  if (wantsGzip) {
    path += ".gz";
    size = m_partition.fileSize(path.c_str());
    gzip = size.has_value();
    if (!gzip) {
      path.resize(path.size() - 3);
    }
  }
  if (!gzip) {
    size = m_partition.fileSize(path.c_str());
    if (!size) {
      if (!wantsGzip) {
        path += ".gz";
        if (m_partition.fileSize(path.c_str())) {
          return sendNotAcceptable(req);
        }
      }
      // Unknown path -> captive redirect (matches serveStatic default-file fallthrough).
      return RFC8908::EmitRedirect(req);
    }
  }

  // The two encodings are different representations, so they get different tags. Keep the quoted tag alive until
  // after send.
  std::string etag;
  if (const char* fsHash = getPartitionHash(m_partition.partition())) {
    etag = std::string("\"") + fsHash + (gzip ? "-gz\"" : "\"");
  }

  Representation rep {
    .contentType  = contentTypeForPath(path),
    .gzip         = gzip,
    .cacheControl = kCacheControl,
    .etag         = etag.empty() ? nullptr : etag.c_str(),
    .size         = *size,
  };

  if (matchesIfNoneMatch(req, rep.etag)) {
    return sendNotModified(req, rep);
  }
  if (req->method == HTTP_HEAD) {
    return sendHeadResponse(req, rep);
  }

  // Headers are only committed with the first chunk, so a failure before anything is on the wire (open or buffer
  // allocation) can still be answered with a clean 500 instead of a dropped connection.
  bool started = false;
  auto begin   = [&]() {
    setHeaders(req, rep);
    if (rep.gzip) {
      httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    started = true;
  };
  bool ok = m_partition.readStream(path.c_str(), [&](std::span<const uint8_t> chunk) {
    if (!started) {
      begin();
    }
    return httpd_resp_send_chunk(req, reinterpret_cast<const char*>(chunk.data()), chunk.size()) == ESP_OK;
  });
  if (!ok) {
    if (started) {
      return ESP_FAIL;  // Mid-stream: the client sees a truncated chunked body
    }
    return sendResp(req, S500, HTTP::ContentType::TextPlain, "Internal Server Error\n");
  }
  if (!started) {
    begin();  // Empty file
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}
