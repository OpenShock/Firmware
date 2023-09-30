#include "FileUtils.h"

#include <LittleFS.h>
#include <WString.h>

const char* const TAG = "FileUtils";

using namespace OpenShock;

bool FileUtils::TryWriteFile(const char* path, const std::uint8_t* data, std::size_t size, bool overwrite) {
  if (LittleFS.exists(path)) {
    if (!overwrite) {
      return false;
    }
    LittleFS.remove(path);
  }

  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", path);

    return false;
  }

  file.write(data, size);
  file.close();

  return true;
}

bool FileUtils::TryWriteFile(const char* path, const String& str, bool overwrite) {
  return TryWriteFile(path,
                      reinterpret_cast<const std::uint8_t*>(str.c_str()),
                      str.length()
                        + 1,  // WARNING: Assumes null-terminated string, could be dangerous (But in practice I think it's fine)
                      overwrite);
}

bool FileUtils::TryReadFile(const char* path, String& str) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", path);

    return false;
  }

  str = file.readString();
  file.close();

  return true;
}

bool FileUtils::DeleteFile(const char* path) {
  return LittleFS.remove(path);
}
