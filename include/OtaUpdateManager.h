#pragma once

#include "OtaUpdateChannel.h"
#include "StringView.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

/*
ok so uhhh:

flash firmware
check firmware hash

flash filesystem
check filesystem hash
check filesystem mountability

restart

check setup success
  rollback if failed
mark ota succeeded
*/

namespace OpenShock::OtaUpdateManager {
  bool Init();

  struct FirmwareRelease {
    std::string appBinaryUrl;
    std::uint8_t appBinaryHash[32];
    std::string filesystemBinaryUrl;
    std::uint8_t filesystemBinaryHash[32];
  };

  bool TryGetFirmwareVersion(OtaUpdateChannel channel, std::string& version);
  bool TryGetFirmwareBoards(const std::string& version, std::vector<std::string>& boards);
  bool TryGetFirmwareRelease(const std::string& version, FirmwareRelease& release);

  bool TryStartFirmwareInstallation(OpenShock::StringView version);

  bool IsValidatingApp();
  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager