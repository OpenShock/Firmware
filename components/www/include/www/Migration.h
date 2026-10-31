#pragma once

// The www half of the 1.x -> 2.x storage migration (docs/STORAGE.md): write the www image over the 1.x littlefs
// `static0` partition and retype it as the `www` partition, in place. Run by the storage migration task
// (migration/StorageMigration.h).

#include <cstdint>
#include <string_view>

namespace OpenShock::HTTP {
  class Client;
}

namespace OpenShock::Www {
  enum class MigrationResult {
    NotNeeded,  // the table already has `www`
    Refused,    // nothing was touched: no 1.x littlefs `static0` partition, or the table could not be read
    Failed,     // the image did not download or verify: static0 holds neither the 1.x assets nor a www image, so the
                // portal has no assets until a retry succeeds
  };

  /// @brief Downloads the www image at `url` over `static0`, checks it against `sha256` (the release's `staticfs`
  ///        artifact hash) and that it parses, then retypes `static0` as `www` and restarts, whether the table write
  ///        went through or not (PartitionTable::Replace). Returns only when it did not restart.
  ///
  /// Requires the captive portal to be closed (it serves from static0) and no OTA install to be running (it writes
  /// static0 too). A power cut between the erase and the write of the table sector bricks the hub until the table is
  /// reflashed over USB (docs/STORAGE.md, Recovery).
  MigrationResult MigrateToWwwPartition(HTTP::Client& client, std::string_view url, const uint8_t (&sha256)[32]);
}  // namespace OpenShock::Www
