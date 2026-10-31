#pragma once

// The config half of the 1.x -> 2.x storage migration (docs/STORAGE.md): retype the 1.x littlefs `config` partition
// as the `cfg` NVS store, in place. Not called by anything yet; the release that pipelines the storage migrations
// decides when it runs.
//
// What happens around it is Store's: before it, a 1.x hub keeps its config (imported from the 1.x file) in the shared
// `nvs`; on the first boot after it, Store erases the littlefs left in `cfg` and moves the config over.

namespace OpenShock::Config {
  enum class MigrationResult {
    NotNeeded,  // the table already has `cfg`
    Refused,    // no 1.x `config` partition, the table could not be read, or the config is not in the shared nvs
                // (retyping would lose it)
  };

  /// @brief Retypes the 1.x `config` partition as `cfg` and restarts, whether the table write went through or not
  ///        (PartitionTable::Replace). Returns only when it touched nothing.
  ///
  /// Requires everything that uses NVS or the partition API outside the config store to be stopped: it closes NVS
  /// for the table write. A power cut between the erase and the write of the table sector bricks the hub until the
  /// table is reflashed over USB (docs/STORAGE.md, Recovery).
  MigrationResult MigrateToCfgPartition();
}  // namespace OpenShock::Config
