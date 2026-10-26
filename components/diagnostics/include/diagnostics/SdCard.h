#pragma once

#include <cstdint>

// Logging to a removable SD card, compiled in only with CONFIG_OPENSHOCK_DIAG_SDCARD. Without it these functions
// exist but report Disabled and do nothing. The card format is documented in docs/SDCARD.md.
namespace OpenShock::Diagnostics::SdCard {
  enum class State : uint8_t {
    Disabled,     // Not compiled in, or not configured
    NoCard,       // Card detect says the slot is empty
    Unusable,     // A card is present but could not be initialised, or is smaller than 1 GiB
    NeedsFormat,  // Blank or foreign: never written until someone asks for a format
    Recovering,   // Full scan of a damaged ring in progress
    Mounted,
    Ejected,      // Safely removed; take the card out
    Failed,       // Writes failed twice; reinsert the card
  };

  const char* StateName(State state);

  struct Info {
    State state;
    uint32_t cardSectors;
    uint32_t epoch;
    uint64_t nextSeq;
    uint32_t boot;
    uint32_t droppedRecords;  // Since boot
    uint32_t droppedBytes;
    uint64_t nextDumpSeq;
  };
  Info GetInfo();

  /// @brief Formats the card in the slot, destroying what it holds, then mounts it. Blocks until done.
  bool Format();

  /// @brief Flushes, writes UNMOUNT and shuts the card down so it can be removed. Blocks until done.
  bool Eject();

  /// @brief Records wall-clock time (TIME_SYNC), so the reader can turn uptimes into dates.
  enum class TimeSource : uint8_t {
    Ntp     = 1,
    Backend = 2,
    Rtc     = 3,
    Manual  = 4,
  };
  void RecordTimeSync(int64_t unixUs, TimeSource source);
}  // namespace OpenShock::Diagnostics::SdCard
