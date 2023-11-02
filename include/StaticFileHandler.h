#pragma once

#include <ESPAsyncWebServer.h>

namespace OpenShock {
  class StaticFileHandler : public AsyncWebHandler {
  public:
    StaticFileHandler();
    ~StaticFileHandler();

    bool ok() const { return m_ok; }

    bool canHandle(AsyncWebServerRequest *request) override;
    void handleRequest(AsyncWebServerRequest *request) override;
  private:
    bool m_ok;
  };
} // namespace OpenShock

