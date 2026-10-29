#pragma once

#include "enums/FirmwareBootType.h"
#include "enums/OtaUpdateChannel.h"
#include "SemVer.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenShock::HTTP {
  class Client;
}

namespace OpenShock::OtaUpdateManager {
  [[nodiscard]] bool Init();

  struct FirmwareRelease {
    std::string appBinaryUrl;
    uint8_t appBinaryHash[32];
    std::string filesystemBinaryUrl;  // the `staticfs` artifact: the www image (littlefs for 1.x-numbered releases)
    uint8_t filesystemBinaryHash[32];
    std::string partitionTableUrl;    // the `partitions` artifact (this board's partition-table.bin); empty if none
    uint8_t partitionTableHash[32];
  };

  /// @brief Asks the repository server for the head of a channel for this board.
  /// @return The version to install, or std::nullopt on failure. When the hub is already current the server
  ///         answers 204 and this is the running version, so an equality check skips the update.
  [[nodiscard]] std::optional<OpenShock::SemVer> TryGetFirmwareVersion(HTTP::Client& client, OtaUpdateChannel channel);

  /// @brief Fetches the app and staticfs artifacts for a specific version of this board.
  [[nodiscard]] std::optional<FirmwareRelease> TryGetFirmwareRelease(HTTP::Client& client, const OpenShock::SemVer& version);

  bool TryStartFirmwareUpdate(const OpenShock::SemVer& version);

  /// @brief Installs `version` on the hub's own initiative: the last step of the partition layout migration
  ///        (components/migration). Unlike TryStartFirmwareUpdate it is not a backend request, so neither the
  ///        backend-management nor the manual-approval setting holds it back; disabling OTA updates still does.
  bool TryStartMigrationUpdate(const OpenShock::SemVer& version);

  /// @brief Keeps installs out while the caller writes flash an install also writes (the layout migration's www image
  ///        and partition table). False while an install is running. Held by the calling task until
  ///        UnlockFlashWrites() or a restart; an install that comes up meanwhile is refused, not queued.
  [[nodiscard]] bool TryLockFlashWrites();
  void UnlockFlashWrites();

  /// @brief Asks the update task to check the configured channel for a new version as soon as it can.
  bool RequestUpdateCheck();

  FirmwareBootType GetFirmwareBootType();
  bool IsValidatingApp();

  /// @brief On the first boot of a freshly installed image, whether it was meant for this board.
  ///
  /// Compares the running image's board descriptor with the one in the slot it was installed from. The update
  /// path already refuses a foreign image before it is made bootable; this is the second line, for an image that
  /// got into the slot some other way. A slot without a descriptor (a pre-descriptor build, or empty) cannot be
  /// compared and passes.
  /// @return false when the previous slot names a different board, in which case the caller should roll back.
  bool IsPendingImageForThisBoard();

  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
