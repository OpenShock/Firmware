#pragma once

// Host-test stub for fs/LfsPartition.h: an in-memory file store instead of littlefs on a flash partition, so the
// tests can stand in the config file a 1.x hub left behind (HostLfs::files()) and see that the import never writes
// to it. Every LfsPartition instance shares one store.
#include "OpenShock.h"
#include "util/FunctionRef.h"

#include <esp_partition.h>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace OpenShock {
  struct HostLfs {
    static std::map<std::string, std::vector<uint8_t>>& files()
    {
      static std::map<std::string, std::vector<uint8_t>> s_files;
      return s_files;
    }
    static inline bool mountable = true;  // false: the partition holds no mountable image
    static inline int writeCount = 0;     // writes through the stub: the legacy import must never make one

    static void reset()
    {
      files().clear();
      mountable  = true;
      writeCount = 0;
    }
  };

  class LfsPartition {
    DISABLE_COPY(LfsPartition);
    DISABLE_MOVE(LfsPartition);

  public:
    LfsPartition() = default;

    bool mount(const esp_partition_t* partition, bool writable, bool formatOnFail = false)
    {
      (void)writable;
      (void)formatOnFail;
      m_mounted = partition != nullptr && HostLfs::mountable;
      return m_mounted;
    }
    void unmount() { m_mounted = false; }
    bool isMounted() const { return m_mounted; }

    bool exists(const char* path) { return m_mounted && HostLfs::files().contains(path); }
    bool readStream(const char* path, Util::FunctionRef<bool(std::span<const uint8_t>)> sink)
    {
      std::vector<uint8_t> data;
      return readAll(path, data) && sink(data);
    }
    bool readAll(const char* path, std::vector<uint8_t>& out)
    {
      auto it = HostLfs::files().find(path);
      if (!m_mounted || it == HostLfs::files().end()) {
        out.clear();
        return false;
      }
      out = it->second;
      return true;
    }
    bool writeAll(const char* path, std::span<const uint8_t> data)
    {
      ++HostLfs::writeCount;
      HostLfs::files()[path].assign(data.begin(), data.end());
      return true;
    }
    bool remove(const char* path)
    {
      ++HostLfs::writeCount;
      return HostLfs::files().erase(path) != 0;
    }

  private:
    bool m_mounted = false;
  };
}  // namespace OpenShock
