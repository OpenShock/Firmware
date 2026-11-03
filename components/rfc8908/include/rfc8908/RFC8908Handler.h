#pragma once

#include <esp_http_server.h>

namespace OpenShock::RFC8908 {
  // Registers the OS captive-detection surface on an esp_http_server instance:
  //   - the RFC 8908 endpoint /captive-portal/api (or /captiveportal/api) -> application/captive+json
  // Connectivity probes (/generate_204, /hotspot-detect.html, ...) get no routes: the portal's routes redirect any
  // captured request (IsCaptured), and the static file handler's HandleMiss catches the same probes sent to our
  // address. The full list is in the .cpp.
  //
  // All of this applies only to connections that came in over the portal AP. The same server also answers on the
  // hub's LAN address, where it is a plain web server: no redirects, and the RFC 8908 endpoint answers 404.
  //
  // `apIpv4` is the portal AP address (e.g. "4.3.2.1"); the portal URL and the AP check are built from it once, here.
  esp_err_t RegisterProbeHandlers(httpd_handle_t server, const char* apIpv4);

  // True when the request came in over the portal AP and its Host header is a name rather than an IP literal. On the
  // AP our own pages are always requested by IP, so a name is one the wildcard DNS server sent here: a connectivity
  // probe or a user browsing elsewhere, which should get the captive redirect whatever the path. On the LAN a name
  // is normal (the hub's DHCP hostname in the router's DNS) and never counts.
  bool IsCaptured(httpd_req_t* req);

  // Emit the standard captive redirect (302 -> http://<apIpv4>/).
  esp_err_t EmitRedirect(httpd_req_t* req);

  // Answer a request the server has nothing for: the captive redirect if it came in over the AP and is addressed to
  // another host or is a known connectivity probe path, otherwise a plain 404. For a catch-all handler's not-found
  // branch.
  esp_err_t HandleMiss(httpd_req_t* req);
}  // namespace OpenShock::RFC8908
