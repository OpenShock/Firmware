#pragma once

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

  void FlashFirmware();
  void ValidateFirmware();

  void FlashFilesystem();
  void ValidateFilesystem();

  bool IsValidatingImage();
  void InvalidateAndRollback();
  void ValidateImage();
}  // namespace OpenShock::OtaUpdateManager
