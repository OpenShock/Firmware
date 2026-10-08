#pragma once

#include "fs/LfsPartition.h"

#include "OpenShock.h"

#include <esp_partition.h>

#include <cstdint>
#include <functional>
#include <span>

namespace OpenShock {
  // Read-only view of a littlefs partition — for pre-built images (e.g. the captive
  // portal's gzipped web assets) that are flashed once and only read at runtime.
  // Streams files out to a caller-provided sink; never writes.
  class StaticFs {
    DISABLE_COPY(StaticFs);
    DISABLE_MOVE(StaticFs);

  public:
    StaticFs() = default;

    bool mount(const esp_partition_t* partition) { return m_fs.mount(partition, false); }
    void unmount() { m_fs.unmount(); }
    bool isMounted() const { return m_fs.isMounted(); }

    bool exists(const char* path) { return m_fs.exists(path); }
    bool readFile(const char* path, const std::function<bool(std::span<const uint8_t>)>& sink) { return m_fs.readStream(path, sink); }

  private:
    LfsPartition m_fs;
  };
}  // namespace OpenShock
