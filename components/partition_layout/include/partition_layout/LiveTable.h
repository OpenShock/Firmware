#pragma once

// The device side of PartitionTable.h: what this hub's flash holds right now. Not built for the host tests.

#include "partition_layout/PartitionTable.h"

#include <array>
#include <cstdint>
#include <optional>

namespace OpenShock::PartitionTable {
  /// @brief The raw table at 0x8000; false when the flash read fails.
  bool ReadLiveBytes(std::array<uint8_t, kMaxLen>& out);

  /// @brief The table at 0x8000, parsed; nullopt (logged) when it cannot be read or does not parse.
  [[nodiscard]] std::optional<Table> ReadLive();

  /// @brief The layout of the table at 0x8000; Unknown when it cannot be read.
  [[nodiscard]] Layout LiveLayout();

  /// @brief The flash size the bootloader's image header declares, in bytes; 0 when it cannot be read. The
  ///        bootloader refuses every partition past it.
  [[nodiscard]] uint32_t BootloaderFlashSize();
}  // namespace OpenShock::PartitionTable
