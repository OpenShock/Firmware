#pragma once

#include "FirmwareBootType.h"
#include "SemVer.h"

namespace OpenShock::OtaUpdateManager {
  [[nodiscard]] bool Init();

  bool TryStartFirmwareUpdate(const OpenShock::SemVer& version);

  FirmwareBootType GetFirmwareBootType();
  bool IsValidatingApp();

  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
