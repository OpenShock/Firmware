#pragma once

// Host-test stub for fs/ConfigFs.h: an in-memory file store instead of littlefs on
// a flash partition. Every ConfigFs instance shares one store, so tests can seed
// the file Config::Init loads and inspect what Config writes (see HostConfigFs).
#include "OpenShock.h"

#include <esp_partition.h>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace OpenShock {
  struct HostConfigFs {
    static std::map<std::string, std::vector<uint8_t>>& files()
    {
      static std::map<std::string, std::vector<uint8_t>> s_files;
      return s_files;
    }
    static inline bool failWrites = false;
    static inline int writeCount  = 0;

    static void reset()
    {
      files().clear();
      failWrites = false;
      writeCount = 0;
    }
  };

  class ConfigFs {
    DISABLE_COPY(ConfigFs);
    DISABLE_MOVE(ConfigFs);

  public:
    ConfigFs() = default;

    bool mount(const esp_partition_t* partition) { return partition != nullptr; }
    void unmount() { }
    bool isMounted() const { return true; }

    bool exists(const char* path) { return HostConfigFs::files().contains(path); }
    bool read(const char* path, std::vector<uint8_t>& out)
    {
      auto it = HostConfigFs::files().find(path);
      if (it == HostConfigFs::files().end()) {
        return false;
      }
      out = it->second;
      return true;
    }
    bool write(const char* path, std::span<const uint8_t> data)
    {
      if (HostConfigFs::failWrites) {
        return false;
      }
      ++HostConfigFs::writeCount;
      HostConfigFs::files()[path].assign(data.begin(), data.end());
      return true;
    }
    bool remove(const char* path) { return HostConfigFs::files().erase(path) != 0; }
  };
}  // namespace OpenShock
