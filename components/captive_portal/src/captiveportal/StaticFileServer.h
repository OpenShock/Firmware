#pragma once

#include "OpenShock.h"
#include "www/WwwPartition.h"

#include <esp_http_server.h>

#include <string>
#include <string_view>

namespace OpenShock::CaptivePortal {
  /// @brief Serves the portal UI from the flat www image (mapped, served by pointer), mounted for this object's lifetime.
  class StaticFileServer {
    DISABLE_COPY(StaticFileServer);
    DISABLE_MOVE(StaticFileServer);

  public:
    StaticFileServer();

    /// @brief Registers the catch-all route, so call this last: earlier routes win. Stop the server before destroying this.
    void registerHandler(httpd_handle_t server);

  private:
    static esp_err_t missingFsHandler(httpd_req_t* req);
    static esp_err_t staticFileHandler(httpd_req_t* req);
    esp_err_t serveFromImage(httpd_req_t* req, std::string_view uri, bool wantsGzip);

    Www::Partition m_www;
    std::string m_etag;  // the www image's hash, quoted
  };
}  // namespace OpenShock::CaptivePortal
