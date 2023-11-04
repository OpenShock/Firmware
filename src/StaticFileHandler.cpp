#include "StaticFileHandler.h"

#include "Time.h"

#include <esp_task_wdt.h>
#include <LittleFS.h>

#include <string>
#include <unordered_map>

const char* const TAG = "StaticFileHandler";

namespace std {
  // hash functor for Arduino String
  template<>
  struct hash<String> {
    // hash _Keyval to size_t value by pseudorandomizing transform
    size_t operator()(const String& _Keyval) const noexcept { return (std::_Hash_bytes(_Keyval.c_str(), _Keyval.length(), 0)); }
  };
}  // namespace std

std::unordered_map<String, const __FlashStringHelper*> mimeTypes = {
  { ".html",              F("text/html")},
  {  ".htm",              F("text/html")},
  {  ".css",               F("text/css")},
  { ".json",       F("application/json")},
  {   ".js", F("application/javascript")},
  {  ".png",              F("image/png")},
  {  ".gif",              F("image/gif")},
  {  ".jpg",             F("image/jpeg")},
  {  ".ico",           F("image/x-icon")},
  {  ".svg",          F("image/svg+xml")},
  {  ".eot",               F("font/eot")},
  { ".woff",              F("font/woff")},
  {".woff2",             F("font/woff2")},
  {  ".ttf",               F("font/ttf")},
  {  ".xml",               F("text/xml")},
  {  ".pdf",        F("application/pdf")},
  {  ".zip",        F("application/zip")},
  {   ".gz",     F("application/x-gzip")},
  {  ".txt",             F("text/plain")},
};

using namespace OpenShock;

String GetContentType(const String& path) {
  const char* const MIME_DEFAULT = "application/octet-stream";

  int lastDot = path.lastIndexOf('.');
  if (lastDot < 0) {
    return MIME_DEFAULT;
  }

  String extension = path.substring(lastDot);

  auto it = mimeTypes.find(extension);
  if (it == mimeTypes.end()) {
    return MIME_DEFAULT;
  }

  return String(it->second);
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
  CustomAsyncFileResponse(fs::File file, const String& fileName, bool gzipped) : AsyncAbstractResponse(), m_file(file), m_started(OpenShock::millis()), m_lastRead(0) {
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
