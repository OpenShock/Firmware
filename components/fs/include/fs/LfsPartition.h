#pragma once

#include "OpenShock.h"

#include <esp_partition.h>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace OpenShock {
  namespace detail {
    struct LfsState;  // defined in LfsPartition.cpp — keeps lfs.h out of public headers
  }

  // Mounts a littlefs filesystem directly over a raw esp_partition using the vendored
  // littlefs core — no VFS registration. This is the shared kernel behind StaticFs
  // (read-only) and ConfigFs (read/write); most callers should use one of those
  // façades rather than this class directly.
  //
  // Not thread-safe: callers that share a partition across tasks must serialize access
  // themselves.
  class LfsPartition {
    DISABLE_COPY(LfsPartition);
    DISABLE_MOVE(LfsPartition);

  public:
    LfsPartition();
    ~LfsPartition();

    // Mount `partition`. When `writable` is false, prog/erase are refused and wear
    // leveling is disabled (correct for a pre-built, read-only image). When `writable`
    // is true and the mount fails, the partition is formatted first if `formatOnFail`.
    bool mount(const esp_partition_t* partition, bool writable, bool formatOnFail = false);
    void unmount();
    bool isMounted() const { return m_mounted; }

    bool exists(const char* path);

    // Stream a file to `sink` in chunks. Returns false if the file is missing/unreadable
    // or if `sink` returns false (which aborts the stream).
    bool readStream(const char* path, const std::function<bool(std::span<const uint8_t>)>& sink);

    // Read a whole file into `out`. Returns false (and clears `out`) on any error.
    bool readAll(const char* path, std::vector<uint8_t>& out);

    // Atomically replace `path` with `data` (write to a temp file, then rename).
    // Writable mounts only.
    bool writeAll(const char* path, std::span<const uint8_t> data);

    // Delete `path`. Writable mounts only.
    bool remove(const char* path);

  private:
    detail::LfsState* m_state;
    bool m_mounted;
  };
}  // namespace OpenShock
