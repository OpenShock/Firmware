#include "StaticFileHandler.h"

#include "Utils/FS.h"

#include <esp_littlefs.h>

const char* const TAG = "StaticFileHandler";

using namespace OpenShock;

StaticFileHandler::StaticFileHandler() {
  if (OpenShock::FS::registerPartition("static", "/static", false, true, false) != ESP_OK) {
    ESP_LOGE(TAG, "PANIC: An Error has occurred while mounting LittleFS (static), restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
}

StaticFileHandler::~StaticFileHandler() {
  OpenShock::FS::unregisterPartition("static");
}

bool StaticFileHandler::canHandle(AsyncWebServerRequest* request) {
  if (request->method() != HTTP_GET) {
    return false;
  }

  return true;
}

const char* GetContentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  else if (path.endsWith(".htm")) return "text/html";
  else if (path.endsWith(".css")) return "text/css";
  else if (path.endsWith(".json")) return "application/json";
  else if (path.endsWith(".js")) return "application/javascript";
  else if (path.endsWith(".png")) return "image/png";
  else if (path.endsWith(".gif")) return "image/gif";
  else if (path.endsWith(".jpg")) return "image/jpeg";
  else if (path.endsWith(".ico")) return "image/x-icon";
  else if (path.endsWith(".svg")) return "image/svg+xml";
  else if (path.endsWith(".eot")) return "font/eot";
  else if (path.endsWith(".woff")) return "font/woff";
  else if (path.endsWith(".woff2")) return "font/woff2";
  else if (path.endsWith(".ttf")) return "font/ttf";
  else if (path.endsWith(".xml")) return "text/xml";
  else if (path.endsWith(".pdf")) return "application/pdf";
  else if (path.endsWith(".zip")) return "application/zip";
  else if(path.endsWith(".gz")) return "application/x-gzip";
  else if (path.endsWith(".txt")) return "text/plain";
  else return "application/octet-stream";
}

class FileStream : public Stream {
public:
  FileStream(FILE* fileHandle, std::size_t fileLength) : m_handle(fileHandle), m_length(fileLength) {}

  int available() override {
    return m_length;
  }

  int read() override {
    if (m_length == 0) {
      return -1;
    }

    _ensureBuffer();

    m_length--;
    return m_buffer[m_offset++];
  }

  int peek() override {
    if (m_length == 0) {
      return -1;
    }

    _ensureBuffer();

    return m_buffer[m_offset];
  }

  std::size_t readBytes(char* buffer, std::size_t length) override {
    if (m_length == 0) {
      return 0;
    }

    char* start = buffer;
    char* end = buffer + length;

    // First drain the buffer
    std::size_t bufferAvail = sizeof(m_buffer) - m_offset;
    if (bufferAvail > 0) {
      std::size_t nToRead = std::min(length, bufferAvail);

      memcpy(buffer, m_buffer + m_offset, nToRead);

      buffer += nToRead;
    }

    // Then read the rest directly from the file
    if (buffer < end) {
      buffer += fread(buffer, 1, end - buffer, m_handle);
    }

    std::size_t nRead = buffer - start;

    m_length -= nRead;

    return nRead;
  }
private:
void _ensureBuffer() {
    if (m_length == 0 || sizeof(m_buffer) - m_offset > 0) {
      return;
    }

    m_length -= fread(m_buffer, 1, sizeof(m_buffer), m_handle);
    m_offset = 0;
  }

  FILE* m_handle;
  std::size_t m_length;
  std::size_t m_offset;
  char m_buffer[1024];
};

void StaticFileHandler::handleRequest(AsyncWebServerRequest* request) {
  if (!ok()) {
    request->send(500);
    return;
  }

  auto url = request->url();

  // Handle path (every static file should be gzipped)
  if (url == "/") {
    url = "/static/index.html.gz";
  } else {
    url = "/static" + url + ".gz";

    if (access(url.c_str(), R_OK) != 0) {
      url = "/static" + url;
    }
  }

  FILE* handle = fopen(url.c_str(), "rb");
  if (!handle) {
    request->send(404, "text/plain", "Not found");
    return;
  }

  fseek(handle, 0, SEEK_END);
  long size = ftell(handle);
  fseek(handle, 0, SEEK_SET);

  String contentType = GetContentType(url);

  FileStream* stream = new FileStream(handle);

  auto response = request->beginResponse(*stream, contentType, size);

  response->setCode(200);
  response->addHeader("Content-Encoding", "gzip");

  request->send(response);

  fclose(handle);

  delete stream;
}
