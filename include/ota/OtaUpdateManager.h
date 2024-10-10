#pragma once

#include "FirmwareBootType.h"
#include "FirmwareReleaseInfo.h"
#include "OtaUpdateChannel.h"
#include "SemVer.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenShock::OtaUpdateManager {
  [[nodiscard]] bool Init();

  bool TryGetFirmwareRelease(const OpenShock::SemVer& version, FirmwareReleaseInfo& release);

  bool TryStartFirmwareInstallation(const OpenShock::SemVer& version);

  FirmwareBootType GetFirmwareBootType();
  bool IsValidatingApp();

  void InvalidateAndRollback();
  void ValidateApp();
}  // namespace OpenShock::OtaUpdateManager
