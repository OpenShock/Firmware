#pragma once

// Pipelines the two halves of the 1.x -> 2.x storage migration (docs/STORAGE.md) on a hub that still boots the 1.x
// partition table. Each boot starts by looking at the live table, so a restart at any point resumes at the first
// unfinished step:
//
//   1. `config` is still littlefs: Config::MigrateToCfgPartition retypes it as `cfg` and restarts; the next boot's
//      Config::Init erases the littlefs and moves the config in from the shared nvs.
//   2. `static0` is still littlefs: once online, Www::MigrateToWwwPartition writes the www image over it, retypes it
//      as `www` and restarts. The image is the running release's `staticfs` artifact (a 2.x release publishes the
//      www image there); a build with no such release (a dev build, a 1.x-numbered one) waits for `migrate www
//      <url> <sha256>` over the serial console.
//
// Each step takes the OTA flash-write lock for its whole run, so an install never writes static0 or runs while the
// table changes, and backs off while an install is running.

#include <cstdint>
#include <string>

namespace OpenShock::Migration {
  /// @brief Starts the migration task when the live table still has a 1.x partition to migrate; a no-op otherwise.
  ///        Call after setup, so a migration only ever starts from a firmware that came up (and validated itself).
  bool Start();

  /// @brief Whether the migration task is running: the live table still has something to migrate.
  [[nodiscard]] bool IsPending();

  /// @brief Hands the www step an image to use instead of the running release's: the task downloads it on its next
  ///        round. False when no migration is pending.
  bool RequestWww(std::string url, const uint8_t (&sha256)[32]);

  /// @brief Asks the task to run its next round now instead of after its retry delay. False when none is pending.
  bool RequestNow();
}  // namespace OpenShock::Migration
