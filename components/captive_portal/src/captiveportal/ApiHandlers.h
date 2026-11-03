#pragma once

#include <esp_http_server.h>

namespace OpenShock::CaptivePortal {
  class CaptivePortalInstance;

  /// @brief Registers the JSON setup endpoints (/api/*) on `server`. `instance` owns the server and outlives its handlers.
  void RegisterApiHandlers(httpd_handle_t server, CaptivePortalInstance* instance);
}  // namespace OpenShock::CaptivePortal
