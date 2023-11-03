#include "StaticFileHandler.h"

#include "Utils/FS.h"

#include <esp_littlefs.h>

const char* const TAG = "StaticFileHandler";

using namespace OpenShock;

class FileStream : public Stream {
public:
  FileStream(FILE* fileHandle, std::size_t fileLength) : m_fHandle(fileHandle), m_fLeft(fileLength) { }

  std::size_t availableFile() const { return m_fLeft; }
  std::size_t availableBuffer() const { return sizeof(m_buffer) - m_bOffs; }
  std::size_t availableTotal() const { return availableBuffer() + availableFile(); }

  int available() override { return (int)std::min(availableTotal(), (std::size_t)INT_MAX); }

  int read() override {
    if (availableTotal() == 0) {
      return -1;
    }

    _ensureBuffer();

    return m_buffer[m_bOffs++];
  }

  int peek() override {
    if (availableTotal() == 0) {
      return -1;
    }

    _ensureBuffer();

    return m_buffer[m_bOffs];
  }

  std::size_t write(uint8_t) override { return 0; }

  std::size_t readBytes(char* buffer, std::size_t length) override {
    std::size_t nToRead = std::min(availableTotal(), length);

    if (buffer == nullptr || nToRead == 0) {
      return 0;
    }

    char* cur       = buffer;
    const char* end = buffer + nToRead;

    // First drain the buffer
    std::size_t bufferToRead = std::min(availableBuffer(), nToRead);
    if (bufferToRead > 0) {
      const char* bPtr = m_buffer + m_bOffs;
      const char* bEnd = bPtr + bufferToRead;

      std::copy(bPtr, bEnd, cur);

      cur += bufferToRead;
    }

    // Then read the rest directly from the file
    if (cur < end) {
      std::size_t nRead = fread(cur, 1, end - cur, m_fHandle);

      m_fLeft -= nRead;
      cur += nRead;
    }

    return cur - buffer;
  }

private:
  void _ensureBuffer() {
    if (availableFile() == 0 || availableBuffer() > 0) {
      return;
    }

    m_fLeft -= fread(m_buffer, 1, sizeof(m_buffer), m_fHandle);
    m_bOffs = 0;
  }

  FILE* m_fHandle;
  std::size_t m_fLeft;
  std::size_t m_bOffs;
  char m_buffer[1024];
};

StaticFileHandler::StaticFileHandler() : m_fileSystem(FileSystem::GetWWW()) {
  if (!m_fileSystem->ok()) {
    ESP_LOGE(TAG, "An Error has occurred while initializing StaticFileHandler");
    m_fileSystem = nullptr;
  }
}

bool StaticFileHandler::ok() const {
  return m_fileSystem != nullptr;
}
bool StaticFileHandler::canServeFiles() const {
  if (!ok()) {
    return false;
  }

  return m_fileSystem->canRead("/index.html.gz");
}

bool StaticFileHandler::canHandle(AsyncWebServerRequest* request) {
  if (!ok()) {
    return false;
  }

  if (request->method() != HTTP_GET) {
    return false;
  }

  return true;
}

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

void StaticFileHandler::handleRequest(AsyncWebServerRequest* request) {
  if (!ok()) {
    request->send(500);
    return;
  }

  auto url = request->url();

  FILE* handle;

  // Handle path (every static file should be gzipped)
  if (url == "/") {
    handle = fopen("/index.html.gz", "rb");
  } else {
    handle = fopen((url + ".gz").c_str(), "rb");

    // Fallback to non-gzipped file
    if (handle == nullptr) {
      handle = fopen(url.c_str(), "rb");
    }
  }

  // File not found
  if (handle == nullptr) {
    request->send(404, "text/plain", "Not found");
    return;
  }

  fseek(handle, 0, SEEK_END);
  long size = ftell(handle);
  fseek(handle, 0, SEEK_SET);

  // File size is negative
  if (size < 0) {
    request->send(500, "text/plain", "Internal Server Error");
    return;
  }

  String contentType = GetContentType(url);

  FileStream* stream = new FileStream(handle, size);

  auto response = request->beginResponse(*stream, contentType, size);

  response->setCode(200);
  response->addHeader("Content-Encoding", "gzip");

  request->send(response);

  fclose(handle);

  delete stream;
}
