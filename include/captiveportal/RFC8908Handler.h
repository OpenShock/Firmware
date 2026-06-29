#pragma once

#include <ESPAsyncWebServer.h>

namespace OpenShock::CaptivePortal {
  class RFC8908Handler : public AsyncWebHandler {
  public:
    RFC8908Handler()          = default;
    virtual ~RFC8908Handler() = default;

    // Static helper for fallback NotFound behavior
    static void CatchAll(AsyncWebServerRequest* request);

    bool canHandle(AsyncWebServerRequest* request) const override;
    void handleRequest(AsyncWebServerRequest* request) override;
  };
}  // namespace OpenShock::CaptivePortal
