#include <freertos/FreeRTOS.h>

#include "captiveportal/StaticFileServer.h"

const char* const TAG = "CaptivePortalStatic";

#include "captiveportal/HttpUtils.h"
#include "http/ContentTypes.h"
#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "rfc8908/RFC8908Handler.h"

#include <cstring>
#include <span>
#include <string>

using namespace OpenShock;

static const char* getPartitionHash()
{
  const esp_partition_t* partition = FindStaticPartition();
  if (partition == nullptr) {
    return nullptr;
  }

  // Hashing the whole partition takes a noticeable time and its content only changes through OTA, which restarts the
  // device, so compute it once per boot. Only called from the portal manager task.
  static char hash[65];
  static bool hashValid = false;
  if (!hashValid) {
    hashValid = OpenShock::TryGetPartitionHash(partition, hash);
  }

  return hashValid ? hash : nullptr;
}

static const char* contentTypeForPath(const std::string& path)
{
  auto endsWith = [&](const char* ext) {
    size_t n = strlen(ext);
    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
  };

  if (endsWith(".html")) return HTTP::ContentType::TextHTML;
  if (endsWith(".js")) return HTTP::ContentType::JavaScript;
  if (endsWith(".css")) return HTTP::ContentType::CSS;
  if (endsWith(".svg")) return HTTP::ContentType::SVG;
  if (endsWith(".png")) return HTTP::ContentType::PNG;
  if (endsWith(".ico")) return HTTP::ContentType::Icon;
  if (endsWith(".json")) return HTTP::ContentType::JSON;
  if (endsWith(".woff2")) return HTTP::ContentType::WOFF2;
  if (endsWith(".txt")) return HTTP::ContentType::TextPlain;
  return HTTP::ContentType::OctetStream;
}

CaptivePortal::StaticFileServer::StaticFileServer()
{
  // Mount the static filesystem (gzipped portal assets) read-only via raw littlefs.
  const esp_partition_t* partition = FindStaticPartition();
  if (partition == nullptr) {
    OS_LOGE(TAG, "Failed to find static filesystem partition");
  } else if (!m_staticFs.mount(partition)) {
    OS_LOGE(TAG, "Failed to mount static filesystem");
  } else if (!m_staticFs.exists("/www/index.html.gz")) {
    OS_LOGE(TAG, "/www/index.html.gz not found, serving error page");
  } else {
    m_fsHash = getPartitionHash();
    OS_LOGI(TAG, "Serving files from littlefs (hash: %s)", m_fsHash != nullptr ? m_fsHash : "?");
  }
}

void CaptivePortal::StaticFileServer::registerHandler(httpd_handle_t server)
{
  CaptivePortal::registerHandler(server, "/*", HTTP_GET, &StaticFileServer::staticFileHandler, this);
}

esp_err_t CaptivePortal::StaticFileServer::staticFileHandler(httpd_req_t* req)
{
  auto* self = static_cast<StaticFileServer*>(req->user_ctx);

  if (!self->m_staticFs.isMounted()) {
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

  std::string uri(req->uri);
  auto qpos = uri.find('?');
  if (qpos != std::string::npos) {
    uri.resize(qpos);
  }

  // Reject path traversal, then fall through to the captive redirect.
  if (uri.find("..") != std::string::npos) {
    return RFC8908::EmitRedirect(req);
  }

  if (uri == "/") {
    uri = "/index.html";
  }

  // All assets are pre-gzipped, stored at /www/<path>.gz on the static0 partition.
  std::string path = "/www" + uri + ".gz";

  if (!self->m_staticFs.exists(path.c_str())) {
    // Unknown path -> captive redirect (matches serveStatic default-file fallthrough).
    return RFC8908::EmitRedirect(req);
  }

  // ETag / conditional request. Keep the quoted hash alive until after send.
  std::string etag;
  if (self->m_fsHash != nullptr) {
    etag = std::string("\"") + self->m_fsHash + "\"";
    char inm[80];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK && etag == inm) {
      httpd_resp_set_status(req, S304);
      return httpd_resp_send(req, nullptr, 0);
    }
  }

  httpd_resp_set_type(req, contentTypeForPath(uri));
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
  if (!etag.empty()) {
    httpd_resp_set_hdr(req, "ETag", etag.c_str());
  }

  bool ok = self->m_staticFs.readFile(path.c_str(), [req](std::span<const uint8_t> chunk) { return httpd_resp_send_chunk(req, reinterpret_cast<const char*>(chunk.data()), chunk.size()) == ESP_OK; });
  if (!ok) {
    // Headers/chunks may already be on the wire; can't cleanly redirect now.
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}
