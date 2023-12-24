#pragma once

#include "OtaUpdateChannel.h"

#include <string>
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
    std::string version;
    std::string appBinaryUrl;
    std::string appBinaryHash;
    std::string filesystemBinaryUrl;
    std::string filesystemBinaryHash;
  };

  bool TryGetFirmwareVersions(OtaUpdateChannel channel, std::vector<std::string>& versions);
  bool TryGetFirmwareBoards(const std::string& version, std::vector<std::string>& boards);
  bool TryGetFirmwareRelease(const std::string& version, FirmwareRelease& release);

  bool FlashAppPartition(const FirmwareRelease& release);
  bool FlashFilesystemPartition(const FirmwareRelease& release);

  bool IsValidatingApp();
  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
