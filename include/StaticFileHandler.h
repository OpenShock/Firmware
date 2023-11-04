#pragma once

#include <ESPAsyncWebServer.h>

namespace OpenShock {
  class StaticFileHandler : public AsyncWebHandler {
  public:
    StaticFileHandler() = default;

    bool canServeFiles() const;

    bool canHandle(AsyncWebServerRequest* request) override;
    void handleRequest(AsyncWebServerRequest* request) override;
  };
}  // namespace OpenShock
