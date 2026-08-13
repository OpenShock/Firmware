#pragma once

#include "FirmwareBootType.h"
#include "OtaUpdateChannel.h"
#include "SemVer.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenShock::OtaUpdateManager {
  [[nodiscard]] bool Init();

  struct FirmwareRelease {
    std::string appBinaryUrl;
    uint8_t appBinaryHash[32];
    std::string filesystemBinaryUrl;
    uint8_t filesystemBinaryHash[32];
  };

  /// @brief Asks the repository server for the head of a channel for this board.
  /// @param version Receives the version to install. When the hub is already current the server answers
  ///                204 and this is set to the running version, so an equality check skips the update.
  bool TryGetFirmwareVersion(OtaUpdateChannel channel, OpenShock::SemVer& version);

  /// @brief Fetches the app and staticfs artifacts for a specific version of this board.
  bool TryGetFirmwareRelease(const OpenShock::SemVer& version, FirmwareRelease& release);

  bool TryStartFirmwareUpdate(const OpenShock::SemVer& version);

  FirmwareBootType GetFirmwareBootType();
  bool IsValidatingApp();

  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
