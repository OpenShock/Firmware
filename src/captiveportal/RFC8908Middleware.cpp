#include "captiveportal/RFC8908Handler.h"

const char* const TAG = "RFC8908Handler";

#include "Logging.h"

#include <ESPAsyncWebServer.h>

#include <WiFi.h>

#include <cJSON.h>

using namespace OpenShock;

static const char* const captivePortalApiPath = "/captive-portal/api";

static const char* const probeRequestrefixes[] = {"/gen_204", "/generate_204", "/hotspot-detect.", "/success.", "/connecttest.", "/check_network_status.", "/canonical.", "/ncsi.txt", captivePortalApiPath};

static String GetCaptivePortalUrl()
{
  return String("http://") + WiFi.softAPIP().toString() + "/";
}

// Catch-all handler for OS connectivity probes.
// Operating systems attempt to detect internet access by requesting
// well-known URLs. Redirecting these requests to the portal root
// intentionally fails connectivity checks and activates captive
// portal mode.
//
// This mechanism complements the IETF CAPPORT architecture (RFC 8908),
// which provides a machine-readable API for captive state, while this
// handler ensures broad OS compatibility.
void CaptivePortal::RFC8908Handler::CatchAll(AsyncWebServerRequest* request)
{
  AsyncWebServerResponse* response = request->beginResponse(302);
  response->addHeader(asyncsrv::T_LOCATION, GetCaptivePortalUrl());
  response->addHeader(asyncsrv::T_Cache_Control, "no-store, no-cache, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

bool CaptivePortal::RFC8908Handler::canHandle(AsyncWebServerRequest* request) const
{
  if (!request->isHTTP() || request->method() != WebRequestMethod::HTTP_GET) {
    return false;
  }

  auto& url = request->url();

  for (auto& prefix : probeRequestrefixes) {
    if (url.startsWith(prefix)) return true;
  }

  return false;
}
void CaptivePortal::RFC8908Handler::handleRequest(AsyncWebServerRequest* request)
{
  auto& url = request->url();

  // Captive Portal API endpoint (RFC 8908)
  //
  // Provides a machine-readable JSON interface for clients to query
  // their captive portal state. This complements the classic redirect-based
  // captive portal detection used by Android, iOS/macOS, and Windows.
  //
  // Minimal required fields for RFC compliance:
  // - "captive": boolean indicating whether the client is still captive
  // - "user-portal-url": URL of the portal page the client should visit
  //
  // Optional fields that can be added later:
  // - "venue-info-url", "can-extend-session", "seconds-remaining", "bytes-remaining"
  //
  // Clients accessing this endpoint should respect "Cache-Control: no-store"
  // and only rely on this data over HTTPS if possible. On embedded devices,
  // HTTPS may be omitted for practicality, but the redirect-based captive
  // portal ensures broad OS compatibility.
  if (url.equals(captivePortalApiPath)) {
    OS_LOGI(TAG, "Got Captive API request");

    auto portalUrl = GetCaptivePortalUrl();

    cJSON* doc = cJSON_CreateObject();
    cJSON_AddBoolToObject(doc, "captive", true);
    cJSON_AddStringToObject(doc, "user-portal-url", portalUrl.c_str());
    cJSON_AddStringToObject(doc, "venue-info-url", "https://openshock.org");

    AsyncWebServerResponse* response = request->beginResponse(200, "application/captive+json", cJSON_Print(doc));

    response->addHeader("Cache-Control", "private");
    request->send(response);

    cJSON_Delete(doc);
    return;
  }

  // Fallback to 302
  CatchAll(request);
}
