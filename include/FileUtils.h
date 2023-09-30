#pragma once

#include <cstdint>

class String;

namespace OpenShock::FileUtils {
  bool TryWriteFile(const char* path, const std::uint8_t* data, std::size_t size, bool overwrite = true);
  bool TryWriteFile(const char* path, const String& str, bool overwrite = true);

  bool TryReadFile(const char* path, String& str);

  bool DeleteFile(const char* path);
}  // namespace OpenShock::FileUtils
