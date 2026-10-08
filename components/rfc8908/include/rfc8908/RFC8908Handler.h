#pragma once

#include <esp_http_server.h>

namespace OpenShock::RFC8908 {
  // Registers the OS captive-detection surface on an esp_http_server instance:
  //   - well-known probe URLs (/gen_204, /hotspot-detect*, /ncsi.txt, ...) → 302 to the portal
  //   - the RFC 8908 endpoint /captive-portal/api → application/captive+json
  //   - a 404 fallback that redirects any other unknown request to the portal
  //
  // `apIpv4` is the portal AP address (e.g. "4.3.2.1"); the pointer must remain valid
  // for the lifetime of the server (pass a string literal or long-lived buffer).
  //
  // The server must be configured with httpd_uri_match_wildcard for the prefix probe
  // paths to match.
  esp_err_t RegisterProbeHandlers(httpd_handle_t server, const char* apIpv4);

  // Emit the standard captive redirect (302 → http://<apIpv4>/). Exposed so a static
  // file handler can share the exact same fallthrough behaviour on unknown paths.
  esp_err_t EmitRedirect(httpd_req_t* req);
}  // namespace OpenShock::RFC8908
