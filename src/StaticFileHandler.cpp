#include "StaticFileHandler.h"

#include "Time.h"

#include <esp_task_wdt.h>
#include <LittleFS.h>

const char* const TAG = "StaticFileHandler";

using namespace OpenShock;

const char* GetContentType(const String& path) {
  if (path.endsWith(".html"))
    return "text/html";
  else if (path.endsWith(".htm"))
    return "text/html";
  else if (path.endsWith(".css"))
    return "text/css";
  else if (path.endsWith(".json"))
    return "application/json";
  else if (path.endsWith(".js"))
    return "application/javascript";
  else if (path.endsWith(".png"))
    return "image/png";
  else if (path.endsWith(".gif"))
    return "image/gif";
  else if (path.endsWith(".jpg"))
    return "image/jpeg";
  else if (path.endsWith(".ico"))
    return "image/x-icon";
  else if (path.endsWith(".svg"))
    return "image/svg+xml";
  else if (path.endsWith(".eot"))
    return "font/eot";
  else if (path.endsWith(".woff"))
    return "font/woff";
  else if (path.endsWith(".woff2"))
    return "font/woff2";
  else if (path.endsWith(".ttf"))
    return "font/ttf";
  else if (path.endsWith(".xml"))
    return "text/xml";
  else if (path.endsWith(".pdf"))
    return "application/pdf";
  else if (path.endsWith(".zip"))
    return "application/zip";
  else if (path.endsWith(".gz"))
    return "application/x-gzip";
  else if (path.endsWith(".txt"))
    return "text/plain";
  else
    return "application/octet-stream";
}

String GetFsPath(const String& path) {
  if (path.isEmpty()) {
    return "/www/index.html";
  }

  String resolved;

  if (path[0] == '/') {
    resolved = "/www" + path;
  } else {
    resolved = "/www/" + path;
  }

  if (path[path.length() - 1] == '/') {
    resolved = resolved + "index.html";
  }

  return resolved;
}

String GetFileName(const String& path) {
  if (path.isEmpty()) {
    return "index.html";
  }

  int lastSlash = path.lastIndexOf('/');
  if (lastSlash == -1) {
    return path;
  }

  return path.substring(lastSlash + 1);
}

class CustomAsyncFileResponse : public AsyncAbstractResponse {
public:
  CustomAsyncFileResponse(fs::File file, const String& fileName, bool gzipped) : AsyncAbstractResponse(nullptr), m_file(file), m_started(OpenShock::millis()), m_lastRead(0) {
    _contentType   = GetContentType(fileName);
    _contentLength = file.size();

    if (gzipped) {
      addHeader("Content-Encoding", "gzip");
    }
    addHeader("Content-Disposition", "inline; filename=" + fileName);
  }

  ~CustomAsyncFileResponse() { m_file.close(); }

  bool _sourceValid() const { return !!(m_file); }

  virtual size_t _fillBuffer(uint8_t* buf, size_t maxLen) override {
    std::int64_t now = OpenShock::millis();
    if (now - m_started > 10'000) {
      ESP_LOGD(TAG, "File transfer timed out");
      return 0;
    }
    if (now - m_lastRead > 200) {
      m_lastRead = now;
      esp_task_wdt_reset();  // Not today, bitch
    }
    return m_file.read(buf, maxLen);
  }

private:
  fs::File m_file;
  std::int64_t m_started;
  std::int64_t m_lastRead;
};

bool StaticFileHandler::canServeFiles() const {
  return LittleFS.exists("/www/index.html");
}

bool StaticFileHandler::canHandle(AsyncWebServerRequest* request) {
  if (request->method() != HTTP_GET) {
    return false;
  }

  return true;
}

void StaticFileHandler::handleRequest(AsyncWebServerRequest* request) {
  String url    = request->url();
  String fsPath = GetFsPath(url);
  String fsName = GetFileName(fsPath);

  bool gzip     = true;
  fs::File file = LittleFS.open(fsPath + ".gz", "rb");
  if (!file) {
    gzip = false;
    file = LittleFS.open(fsPath, "rb");

    // File not found
    if (!file) {
      ESP_LOGD(TAG, "File %s not found", fsPath.c_str());
      request->send(404, "text/plain", "Not found");
      return;
    }
  }

  request->send(new CustomAsyncFileResponse(file, fsName, gzip));
}
