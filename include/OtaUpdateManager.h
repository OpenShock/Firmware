#pragma once

#include "FirmwareBootType.h"
#include "OtaUpdateChannel.h"
#include "SemVer.h"
#include "StringView.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenShock::OtaUpdateManager {
  bool Init();

  struct FirmwareRelease {
    std::string appBinaryUrl;
    std::uint8_t appBinaryHash[32];
    std::string filesystemBinaryUrl;
    std::uint8_t filesystemBinaryHash[32];
  };

  bool TryGetFirmwareVersion(OtaUpdateChannel channel, OpenShock::SemVer& version);
  bool TryGetFirmwareBoards(const OpenShock::SemVer& version, std::vector<std::string>& boards);
  bool TryGetFirmwareRelease(const OpenShock::SemVer& version, FirmwareRelease& release);

  bool TryStartFirmwareInstallation(const OpenShock::SemVer& version);

  FirmwareBootType GetFirmwareBootType();
  bool IsValidatingApp();

  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
