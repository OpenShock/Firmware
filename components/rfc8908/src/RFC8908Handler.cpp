#include "rfc8908/RFC8908Handler.h"

const char* const TAG = "RFC8908Handler";

#include "Logging.h"
#include "StringHelpers.h"
#include "util/IPv4Utils.h"

#include "http/ContentTypes.h"
#include "json/Json.h"

#include <cstdio>
#include <string>
#include <string_view>

using namespace OpenShock;

// "http://<portal AP IP>/", built once at registration. Handlers are static C callbacks, so it lives here; there is
// only ever one captive portal server at a time. Static storage also satisfies httpd_resp_set_hdr, which keeps the
// pointer until the response is sent.
static char s_portalUrl[sizeof("http://255.255.255.255/")] = "http://4.3.2.1/";
static std::size_t s_portalUrlLen                          = sizeof("http://4.3.2.1/") - 1;

bool RFC8908::IsForeignHost(httpd_req_t* req)
{
  // Long enough for any IPv4 literal with a port. A longer Host comes back truncated, so it is a name (or an IPv6
  // literal, which the '[' check below catches either way).
  char host[sizeof("255.255.255.255:65535")];
  esp_err_t err = httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
  if (err != ESP_OK && err != ESP_ERR_HTTPD_RESULT_TRUNC) {
    return false;  // No Host header (HTTP/1.0): assume it's for us
  }

  std::string_view hostView(host);
  if (StringHasPrefix(hostView, '[')) {
    return false;  // IPv6 literal
  }
  if (err != ESP_OK) {
    return true;
  }

  return !IPv4Utils::IsDottedDecimal(StringBeforeFirst(hostView, ':'));
}

// httpd derives Content-Length from the buffer it is handed and has no bodiless send, so the HEAD response (the GET's
// headers, RFC 9110 §9.3.2) is written out directly.
static esp_err_t SendNotFound(httpd_req_t* req)
{
  static constexpr std::string_view kBody = "Not Found";

  if (req->method != HTTP_HEAD) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, HTTP::ContentType::TextPlain);
    return httpd_resp_send(req, kBody.data(), kBody.size());
  }

  std::string head;
  head.reserve(96);
  head += "HTTP/1.1 404 Not Found\r\nContent-Type: ";
  head += HTTP::ContentType::TextPlain;
  head += "\r\nContent-Length: ";
  head += std::to_string(kBody.size());
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

esp_err_t RFC8908::EmitRedirect(httpd_req_t* req)
{
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", s_portalUrl);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Expires", "0");
  return httpd_resp_send(req, nullptr, 0);
}

// Paths of the OS connectivity probes. These normally arrive addressed to the probe's own host (wildcard DNS sends
// every host to us) and get redirected on that alone; the table catches the same probes sent to our address.
// clang-format off
static constexpr std::string_view kProbePaths[] = {
  "/generate_204",                // Android, ChromeOS, Chrome: connectivitycheck.gstatic.com, connectivitycheck.android.com, www.gstatic.com, clients3.google.com, www.googleapis.cn (China), connect.rom.miui.com (Xiaomi)
  "/generate204",                 // Unknown Android OEM, seen in the field
  "/gen_204",                     // Google
  "/gen204",                      // Variant of the above
  "/captiveportal/generate_204",  // Edge: edge-http.microsoft.com
  "/hotspot-detect.html",         // Apple iOS/macOS: captive.apple.com
  "/connecttest.txt",             // Windows: www.msftconnecttest.com
  "/ncsi.txt",                    // Windows (legacy NCSI): www.msftncsi.com
  "/success.txt",                 // Firefox: detectportal.firefox.com
  "/canonical.html",              // Firefox: detectportal.firefox.com
  "/check_network_status.txt",    // GNOME NetworkManager: nmcheck.gnome.org
};
// clang-format on

static bool IsProbePath(std::string_view path)
{
  for (std::string_view probePath : kProbePaths) {
    if (StringIEquals(path, probePath)) {
      return true;
    }
  }
  return false;
}

bool RFC8908::IsProbeRequest(httpd_req_t* req)
{
  // Path only: httpd leaves the query string in req->uri
  std::string_view path(req->uri);
  return IsProbePath(path.substr(0, path.find('?')));
}

esp_err_t RFC8908::HandleMiss(httpd_req_t* req)
{
  if (IsForeignHost(req) || IsProbeRequest(req)) {
    return EmitRedirect(req);
  }

  // Addressed to us, so a real miss (a stale asset name, a typo): say so instead of answering with the portal
  return SendNotFound(req);
}

// RFC 8908 Captive Portal API endpoint: machine-readable captive state. Complements
// the classic redirect-based detection used by Android, iOS/macOS, and Windows.
// Registered directly rather than through the portal's Host-checked routes: the API is meant to be fetched by the OS
// and only reports public state.
static esp_err_t captiveApiHandler(httpd_req_t* req)
{
  OS_LOGI(TAG, "Got Captive API request");

  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  json_gen_obj_set_bool(gen, "captive", true);
  JSON::objSetString(gen, "user-portal-url", std::string_view(s_portalUrl, s_portalUrlLen));
  if constexpr (sizeof(CONFIG_OPENSHOCK_PORTAL_VENUE_INFO_URL) > 1) {  // empty: leave it out
    JSON::objSetString(gen, "venue-info-url", CONFIG_OPENSHOCK_PORTAL_VENUE_INFO_URL);
  }
  json_gen_end_object(gen);
  std::string jsonStr = writer.finish();

  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/captive+json");
  httpd_resp_set_hdr(req, "Cache-Control", "private");
  return httpd_resp_send(req, jsonStr.data(), jsonStr.size());
}

esp_err_t RFC8908::RegisterProbeHandlers(httpd_handle_t server, const char* apIpv4)
{
  if (apIpv4 != nullptr) {
    int len = snprintf(s_portalUrl, sizeof(s_portalUrl), "http://%s/", apIpv4);
    if (len < 0 || static_cast<std::size_t>(len) >= sizeof(s_portalUrl)) {
      OS_LOGE(TAG, "Not an IPv4 address: %s", apIpv4);
      return ESP_ERR_INVALID_ARG;
    }
    s_portalUrlLen = static_cast<std::size_t>(len);
  }

  // RFC 8908 API. The DHCP option 114 URI (Manager.cpp) points at the first path.
  static const char* const apiPaths[] = {
    "/captive-portal/api",
    "/captiveportal/api",
  };

  esp_err_t result = ESP_OK;
  for (const char* path : apiPaths) {
    httpd_uri_t def = {};
    def.uri         = path;
    def.method      = HTTP_GET;
    def.handler     = captiveApiHandler;
    def.user_ctx    = nullptr;

    esp_err_t err = httpd_register_uri_handler(server, &def);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to register captive API handler %s: %s", path, esp_err_to_name(err));
      result = err;
    }
  }

  return result;
}
