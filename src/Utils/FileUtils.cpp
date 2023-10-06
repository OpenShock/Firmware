#include "Utils/FileUtils.h"

#include <LittleFS.h>
#include <WString.h>

const char* const TAG = "FileUtils";

using namespace OpenShock;

bool FileUtils::TryWriteFile(const char* filename, const std::uint8_t* data, std::size_t size, bool overwrite) {
  if (LittleFS.exists(filename)) {
    if (!overwrite) {
      return false;
    }
    LittleFS.remove(filename);
  }

  File file = LittleFS.open(filename, "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", filename);

    return false;
  }

  bool ok = file.write(data, size) == size;

  file.close();

  return ok;
}

bool FileUtils::TryWriteFile(const char* filename, const String& str, bool overwrite) {
  return TryWriteFile(
    filename,
    reinterpret_cast<const std::uint8_t*>(str.c_str()),
    str.length() + 1,  // WARNING: Assumes null-terminated string, could be dangerous (But in practice I think it's fine)
    overwrite
  );
}

bool FileUtils::TryReadFile(const char* filename, std::uint8_t* data, std::size_t size) {
  File file = LittleFS.open(filename, FILE_READ);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", filename);

    return false;
  }

  bool ok = file.read(data, size) == size;

  file.close();

  return ok;
}

bool FileUtils::TryReadFile(const char* filename, String& str) {
  File file = LittleFS.open(filename, FILE_READ);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", filename);

    return false;
  }

  str = file.readString();
  file.close();

  return true;
}

bool FileUtils::DeleteFile(const char* filename) {
  return LittleFS.remove(filename);
}
