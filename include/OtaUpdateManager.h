#pragma once

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
  void Init();

  enum class FirmwareReleaseChannel {
    Stable,
    Beta,
    Dev,
  };

  struct FirmwareRelease {
    std::string board;
    std::string version;
    std::string appBinaryUrl;
    std::string appBinaryHash;
    std::string filesystemBinaryUrl;
    std::string filesystemBinaryHash;
  };

  bool TryGetFirmwareVersions(FirmwareReleaseChannel channel, std::vector<std::string>& versions);
  bool TryGetFirmwareBoards(const std::string& version, std::vector<std::string>& boards);
  bool TryGetFirmwareRelease(const std::string& version, const std::string& board, FirmwareRelease& release);

  void FlashAppPartition();
  void ValidateAppPartition();

  void FlashFilesystemPartition();
  void ValidateFilesystemPartition();

  bool IsValidatingApp();
  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
