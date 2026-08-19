#pragma once

#include "fs/LfsPartition.h"

#include "OpenShock.h"

#include <esp_partition.h>

#include <cstdint>
#include <span>
#include <vector>

namespace OpenShock {
  // Read/write view of a littlefs partition tailored for a configuration store: whole
  // small blobs, atomic replace (temp file + rename), wear leveling enabled, formats a
  // fresh partition on first mount.
  //
  // Deals only in raw bytes — it has no knowledge of the config serialization format
  // (flatbuffers). Serialization and any cross-task locking belong to the caller.
  class ConfigFs {
    DISABLE_COPY(ConfigFs);
    DISABLE_MOVE(ConfigFs);

  public:
    ConfigFs() = default;

    bool mount(const esp_partition_t* partition) { return m_fs.mount(partition, true, /*formatOnFail=*/true); }
    void unmount() { m_fs.unmount(); }
    bool isMounted() const { return m_fs.isMounted(); }

    bool exists(const char* path) { return m_fs.exists(path); }
    bool read(const char* path, std::vector<uint8_t>& out) { return m_fs.readAll(path, out); }
    bool write(const char* path, std::span<const uint8_t> data) { return m_fs.writeAll(path, data); }
    bool remove(const char* path) { return m_fs.remove(path); }

  private:
    LfsPartition m_fs;
  };
}  // namespace OpenShock
