#pragma once

// The device side of PartitionTable.h: the table this hub's flash holds right now, and the one dangerous write the
// firmware ever makes. Not built for the host tests.

#include "partition_layout/PartitionTable.h"

#include <cstdint>
#include <memory>

namespace OpenShock::PartitionTable {
  /// @brief The raw table at 0x8000; false when the flash read fails.
  bool ReadLiveBytes(Bytes& out);

  /// @brief The table at 0x8000, parsed, on the heap; null (logged) when it cannot be read or does not parse.
  [[nodiscard]] std::unique_ptr<Table> ReadLive();

  enum class ReplaceResult : uint8_t {
    Written,    // `next` is on flash
    Restored,   // `next` would not stick; `live` is back
    Untouched,  // could not even start: the table on flash was never touched
  };

  /// @brief Writes `next` over the table at 0x8000, whose exact bytes are `live`, and reads it back.
  ///
  /// The caller quiesces first (no NVS, nothing else using the partition API) and restarts afterwards whatever the
  /// result: the running firmware keeps the table it loaded at boot until then. A write that fails (an error, a
  /// read-back mismatch) is retried, then `live` is written back, and that is retried for as long as it takes: this
  /// never returns while no table that parses is on flash. A power cut between the erase and the write of that one
  /// sector is the only thing it cannot recover from.
  [[nodiscard]] ReplaceResult Replace(const Bytes& next, const Bytes& live);
}  // namespace OpenShock::PartitionTable
