#pragma once

#include "fs/LfsPartition.h"
#include "OpenShock.h"

#include <esp_http_server.h>

namespace OpenShock::CaptivePortal {
  /// @brief Serves the portal UI: gzipped assets on the static littlefs partition, mounted for this object's lifetime.
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

    LfsPartition m_partition;
  };
}  // namespace OpenShock::CaptivePortal
