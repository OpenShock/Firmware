#pragma once

// The one-time partition layout migration of the 1.9.x pivot release (docs/STORAGE.md): from the 1.x table every
// fielded hub carries to the 2.x table for its flash size, then on to the 2.x firmware.
//
// It only runs in a 1.x-numbered build (the pivot); a 2.x build boots on the 2.x layouts only and this does nothing
// but report. Every step starts by finding out where it is (which layout, which slot, what is installed), so a
// reboot at any point resumes at the first unfinished step:
//
//   on the 1.x table:
//     1. the config is already in the shared nvs (config/Store.h imports the 1.x file on first boot);
//     2. wait for WiFi; fetch the target release; it must carry this board's partition table, and its app and www
//        image must answer;
//     3. download the table into RAM, check its SHA-256, and check it against every invariant
//        (partition_layout/PartitionTable.h); any failure leaves the hub on the 1.x layout and retries later;
//     4. running from the 1.x app1 on 4 MB: copy the image into app0, boot it, start over;
//        running from the 1.x app1 on 8 MB: copy the image into the new app1 (0x400000, flash no 1.x partition uses);
//     5. close the portal and write the target's www image where the new table puts the www partition, so the portal
//        has assets on the first boot of the new layout even without a network;
//     6. quiesce (config writes held, NVS deinitialized), erase and write the table sector, read it back, restart.
//        The erase-to-write window of that one sector is the only step a power cut cannot recover from.
//   on the 2.x table, still running the pivot:
//     7. config/Store.h has moved the config from the shared nvs into cfg; drop the WiFi driver's stale nvs.net80211,
//        erase a stale fragment in the slot the table moved;
//     8. install the target version (OtaUpdateManager::TryStartMigrationUpdate), which boots into it. If it rolls
//        back to the pivot, the chain stops there instead of retrying.

#include <cstdint>

namespace OpenShock::Migration {
  /// @brief Starts the migration task (a no-op in a 2.x build or on a hub with nothing to do). Call after setup.
  bool Start();

  /// @brief The flash size the bootloader's image header declares, in bytes; 0 when it cannot be read. The 1.x
  ///        bootloader refuses every partition past it, so it decides whether the 8 MB table can be used.
  [[nodiscard]] uint32_t BootloaderFlashSize();

  /// @brief The layout of the partition table this hub booted with: "v1 (...)", "v2 (...)" or "unknown".
  [[nodiscard]] const char* LiveLayoutName();
}  // namespace OpenShock::Migration
