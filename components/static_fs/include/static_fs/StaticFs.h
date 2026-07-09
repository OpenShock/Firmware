#pragma once

#include "OpenShock.h"

#include <esp_partition.h>

#include "lfs.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace OpenShock {
  // Read-only littlefs mount over a raw esp_partition, using the vendored littlefs
  // core directly — no VFS registration. Scoped to its owner so it doesn't leave a
  // global mount point around, and so it stays independent of the filesystems other
  // subsystems mount elsewhere.
  class StaticFs {
    DISABLE_COPY(StaticFs);
    DISABLE_MOVE(StaticFs);

  public:
    StaticFs();
    ~StaticFs();

    bool mount(const esp_partition_t* partition);
    void unmount();
    bool isMounted() const { return m_mounted; }

    bool exists(const char* path);

    // Stream a file to `sink` in chunks. Returns false if the file is missing or a
    // read fails, or if `sink` returns false (which aborts the stream).
    bool readFile(const char* path, const std::function<bool(std::span<const uint8_t>)>& sink);

  private:
    static int bdRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size);
    static int bdProg(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size);
    static int bdErase(const struct lfs_config* c, lfs_block_t block);
    static int bdSync(const struct lfs_config* c);

    const esp_partition_t* m_partition;
    lfs_t m_lfs;
    struct lfs_config m_cfg;
    bool m_mounted;
  };
}  // namespace OpenShock
