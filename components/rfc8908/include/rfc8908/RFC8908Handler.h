#pragma once

#include <esp_http_server.h>

namespace OpenShock::RFC8908 {
  // Registers the OS captive-detection surface on an esp_http_server instance:
  //   - the RFC 8908 endpoint /captive-portal/api (or /captiveportal/api) -> application/captive+json
  // Connectivity probes (/generate_204, /hotspot-detect.html, ...) get no routes: the portal's routes redirect any
  // request for another host (IsForeignHost), and the static file handler's HandleMiss catches the same probes sent to
  // our address. The full list is in the .cpp.
  //
  // Assumes the server only accepts connections from the portal AP; the portal refuses the rest when they open.
  //
  // `apIpv4` is the portal AP address (e.g. "4.3.2.1"); the portal URL is built from it once, here.
  esp_err_t RegisterProbeHandlers(httpd_handle_t server, const char* apIpv4);

  // True when the request's Host header is a name rather than an IP literal. On the AP our own pages are always
  // requested by IP, so a name is one the wildcard DNS server sent here: a connectivity probe or a user browsing
  // elsewhere, which should get the captive redirect whatever the path.
  bool IsForeignHost(httpd_req_t* req);

  // Emit the standard captive redirect (302 -> http://<apIpv4>/).
  esp_err_t EmitRedirect(httpd_req_t* req);

  // Answer a request the server has nothing for: the captive redirect if it is addressed to another host or is a
  // known connectivity probe path, otherwise a plain 404. For a catch-all handler's not-found branch.
  esp_err_t HandleMiss(httpd_req_t* req);
}  // namespace OpenShock::RFC8908
