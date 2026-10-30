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

#include <cstddef>
#include <cstring>
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

    if (StringIEquals(coding, "gzip")) {
      return !weightIsZero(params);
    }
    if (coding == "*") {
      wildcard = !weightIsZero(params);
    }
  }
  return wildcard;
}

// Without an Accept-Encoding header any coding may be sent. httpd reports an absent and an empty header the same
// way; the (rare) empty one is treated as absent.
static bool clientAcceptsGzip(httpd_req_t* req)
{
  std::size_t len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
  if (len == 0) {
    return true;
  }
  std::string value;
  value.resize(len + 1);
  if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", value.data(), value.size()) != ESP_OK) {
    return false;
  }
  return acceptsGzip(std::string_view(value.data(), len));
}

CaptivePortal::StaticFileServer::StaticFileServer()
{
  // Mount the static filesystem (gzipped portal assets) read-only via raw littlefs.
  const esp_partition_t* partition = FindStaticPartition();
  if (partition == nullptr) {
    OS_LOGE(TAG, "Failed to find static filesystem partition");
  } else if (!m_partition.mount(partition, false)) {
    OS_LOGE(TAG, "Failed to mount static filesystem");
  } else if (!m_partition.exists("/www/index.html.gz") && !m_partition.exists("/www/index.html")) {
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
  // Decided once: the image is mounted in the constructor and stays that way until destruction
  if (m_partition.isMounted()) {
    CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::staticFileHandler, &m_partition);
  } else {
    CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::missingFsHandler, nullptr);
  }
}

esp_err_t CaptivePortal::StaticFileServer::missingFsHandler(httpd_req_t* req)
{
  // Filesystem image was never uploaded; serve the help page for any request.
  return sendResp(
    req,
    S200,
    HTTP::ContentType::TextPlain,
    R"(You probably forgot to upload the Filesystem with PlatformIO!
Go to PlatformIO -> Platform -> Upload Filesystem Image!
If this happened with a file we provided or you just need help, come to the Discord!

discord.gg/OpenShock
)"
  );
}

esp_err_t CaptivePortal::StaticFileServer::staticFileHandler(httpd_req_t* req)
{
  auto* partition = static_cast<LfsPartition*>(req->user_ctx);

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

  // Assets live at /www/<path>, usually pre-compressed as /www/<path>.gz. A client that accepts gzip is offered the
  // compressed file first; anyone else, or a path with no .gz, gets the plain file if the image has one.
  std::string path;
  path.reserve(4 + uri.size() + (wantsGzip ? 3 : 0));
  path += "/www";
  path += uri;

  bool gzip = false;
  if (wantsGzip) {
    path += ".gz";
    gzip = partition->exists(path.c_str());
    if (!gzip) {
      path.resize(path.size() - 3);
    }
  }
  if (!gzip && !partition->exists(path.c_str())) {
    // Unknown path -> captive redirect (matches serveStatic default-file fallthrough).
    return RFC8908::EmitRedirect(req);
  }

  // ETag / conditional request. The two encodings are different representations, so they get different tags. Keep
  // the quoted tag alive until after send.
  std::string etag;
  if (const char* fsHash = getPartitionHash(partition->partition())) {
    etag = std::string("\"") + fsHash + (gzip ? "-gz\"" : "\"");
    char inm[80];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && etag == inm) {
      httpd_resp_set_status(req, S304);
      return httpd_resp_send(req, nullptr, 0);
    }
  }

  httpd_resp_set_type(req, contentTypeForPath(path));
  if (gzip) {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  }
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
  if (!etag.empty()) {
    httpd_resp_set_hdr(req, "ETag", etag.c_str());
  }

  bool ok = partition->readStream(path.c_str(), [req](std::span<const uint8_t> chunk) { return httpd_resp_send_chunk(req, reinterpret_cast<const char*>(chunk.data()), chunk.size()) == ESP_OK; });
  if (!ok) {
    // Headers/chunks may already be on the wire; can't cleanly redirect now.
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}
