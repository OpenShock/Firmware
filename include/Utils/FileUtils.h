#pragma once

#include <cstdint>
#include <functional>

class String;
namespace fs {
  class File;
}

namespace OpenShock::FileUtils {
  bool TryWriteFile(const char* filename, const std::uint8_t* data, std::size_t size, bool overwrite = true);
  bool TryWriteFile(const char* filename, const String& str, bool overwrite = true);

  bool TryReadFile(const char* filename, std::uint8_t* data, std::size_t size);
  bool TryReadFile(const char* filename, String& str);

  bool DeleteFile(const char* filename);
}  // namespace OpenShock::FileUtils
