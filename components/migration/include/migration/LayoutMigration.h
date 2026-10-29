#pragma once

// The one-time partition layout migration of the 1.9.x pivot release (docs/STORAGE.md): from the 1.x table every
// fielded hub carries to the 2.x table, then on to the 2.x firmware. The 2.x table keeps the 1.x geometry: the
// littlefs `config` becomes the `cfg` NVS store and the littlefs `static0` becomes the flat `www` image, and nothing
// moves, so no firmware image is ever copied.
//
// It only runs in a 1.x-numbered build (the pivot); a 2.x build boots on the 2.x layout only and this does nothing
// but report. Every step starts by finding out where it is (which layout, which slot, what is installed), so a
// reboot at any point resumes at the first unfinished step:
//
//   on the 1.x table:
//     1. the config is already in the shared nvs (config/Store.h imports the 1.x file on first boot);
//     2. wait for WiFi; fetch the target release; it must carry this board's partition table, and its app and www
//        image must answer;
//     3. download the table into RAM, check its SHA-256, and check it against every invariant
//        (partition_layout/PartitionTable.h); any failure leaves the hub on the 1.x layout and retries later;
//     4. close the portal and write the target's www image over the 1.x static0, which the new table makes www, so
//        the portal has assets on the first boot of the new layout even without a network;
//     5. quiesce (config writes held, NVS deinitialized), erase and write the table sector, read it back, restart.
//        The erase-to-write window of that one sector is the only step a power cut cannot recover from.
//   on the 2.x table, still running the pivot:
//     6. config/Store.h has cleared the old littlefs out of cfg and moved the config from the shared nvs into it;
//        drop the WiFi driver's stale nvs.net80211;
//     7. install the target version (OtaUpdateManager::TryStartMigrationUpdate), which boots into it. If it rolls
//        back to the pivot, the chain stops there instead of retrying.

#include <cstdint>

namespace OpenShock::Migration {
  /// @brief Starts the migration task (a no-op in a 2.x build or on a hub with nothing to do). Call after setup.
  bool Start();

  /// @brief The flash size the bootloader's image header declares, in bytes; 0 when it cannot be read. The 1.x
  ///        bootloader refuses every partition past it, so a header smaller than the table refuses the migration.
  [[nodiscard]] uint32_t BootloaderFlashSize();

  /// @brief The layout of the partition table this hub booted with: "v1 (...)", "v2 (...)" or "unknown".
  [[nodiscard]] const char* LiveLayoutName();
}  // namespace OpenShock::Migration
