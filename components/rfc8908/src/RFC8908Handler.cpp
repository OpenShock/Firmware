#include "rfc8908/RFC8908Handler.h"

const char* const TAG = "RFC8908Handler";

#include "Logging.h"

#include "json/Json.h"

#include <string>

using namespace OpenShock;

// The portal AP IP, captured at registration. Handlers are static C callbacks, so we
// keep it here; there is only ever one captive portal server at a time.
static const char* s_apIpv4 = "4.3.2.1";

static const char* const captivePortalApiPath = "/captive-portal/api";

esp_err_t RFC8908::EmitRedirect(httpd_req_t* req)
{
  std::string location = std::string("http://") + s_apIpv4 + "/";

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", location.c_str());
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Expires", "0");
  return httpd_resp_send(req, nullptr, 0);
}

// Catch-all handler for OS connectivity probes. Operating systems attempt to detect
// internet access by requesting well-known URLs; redirecting these to the portal root
// intentionally fails the connectivity check and activates captive-portal mode.
static esp_err_t probeHandler(httpd_req_t* req)
{
  return RFC8908::EmitRedirect(req);
}

// RFC 8908 Captive Portal API endpoint — machine-readable captive state. Complements
// the classic redirect-based detection used by Android, iOS/macOS, and Windows.
static esp_err_t captiveApiHandler(httpd_req_t* req)
{
  OS_LOGI(TAG, "Got Captive API request");

  std::string portalUrl = std::string("http://") + s_apIpv4 + "/";

  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  json_gen_obj_set_bool(gen, "captive", true);
  JSON::objSetString(gen, "user-portal-url", portalUrl.c_str());
  JSON::objSetString(gen, "venue-info-url", "https://openshock.org");
  json_gen_end_object(gen);
  std::string jsonStr = writer.finish();

  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/captive+json");
  httpd_resp_set_hdr(req, "Cache-Control", "private");
  return httpd_resp_send(req, jsonStr.data(), jsonStr.size());
}

static esp_err_t err404Handler(httpd_req_t* req, httpd_err_code_t)
{
  return RFC8908::EmitRedirect(req);
}

esp_err_t RFC8908::RegisterProbeHandlers(httpd_handle_t server, const char* apIpv4)
{
  if (apIpv4 != nullptr) {
    s_apIpv4 = apIpv4;
  }

  // Well-known OS probe paths. Prefix forms (e.g. "/hotspot-detect*") require the
  // server to be configured with httpd_uri_match_wildcard.
  static const char* const probePaths[] = {
    captivePortalApiPath,  // handled specially below
    "/gen_204",
    "/generate_204",
    "/ncsi.txt",
    "/hotspot-detect*",
    "/success*",
    "/connecttest*",
    "/check_network_status*",
    "/canonical*",
  };

  esp_err_t result = ESP_OK;
  for (const char* path : probePaths) {
    httpd_uri_t def = {};
    def.uri         = path;
    def.method      = HTTP_GET;
    def.handler     = (path == captivePortalApiPath) ? captiveApiHandler : probeHandler;
    def.user_ctx    = nullptr;

    esp_err_t err = httpd_register_uri_handler(server, &def);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to register probe handler %s: %s", path, esp_err_to_name(err));
      result = err;
    }
  }

  // Any other unknown request also redirects to the portal.
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, err404Handler);

  return result;
}
