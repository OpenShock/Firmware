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

  bool TryGetFirmwareVersion(OtaUpdateChannel channel, OpenShock::SemVer& version);
  bool TryGetFirmwareBoards(const OpenShock::SemVer& version, std::vector<std::string>& boards);
  bool TryGetFirmwareRelease(const OpenShock::SemVer& version, FirmwareRelease& release);

  bool TryStartFirmwareUpdate(const OpenShock::SemVer& version);

  FirmwareBootType GetFirmwareBootType();
  bool IsValidatingApp();

  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
