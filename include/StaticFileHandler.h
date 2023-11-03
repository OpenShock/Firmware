#pragma once

#include "Utils/FS.h"

#include <ESPAsyncWebServer.h>

namespace OpenShock {
  class StaticFileHandler : public AsyncWebHandler {
  public:
    StaticFileHandler();

    bool ok() const;
    bool canServeFiles() const;

    bool canHandle(AsyncWebServerRequest* request) override;
    void handleRequest(AsyncWebServerRequest* request) override;

  private:
    std::shared_ptr<FileSystem> m_fileSystem;
  };
}  // namespace OpenShock
