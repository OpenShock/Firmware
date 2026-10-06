#pragma once

#include "enums/FirmwareBootType.h"
#include "SemVer.h"

#include <cstdint>
#include <span>

// core sits below the captive_portal and ota components, so it cannot call them directly.
// They register these callbacks from their Init(); until then each call behaves as if the
// subsystem were not running (broadcasts fail, requests are dropped).
namespace OpenShock::AppHooks {
  typedef bool (*BroadcastMessageBINFn)(std::span<const uint8_t> data);
  typedef void (*SetAlwaysEnabledFn)(bool alwaysEnabled);
  typedef FirmwareBootType (*GetFirmwareBootTypeFn)();
  typedef bool (*TryStartFirmwareUpdateFn)(const OpenShock::SemVer& version);

  void RegisterCaptivePortal(BroadcastMessageBINFn broadcastMessageBIN, SetAlwaysEnabledFn setAlwaysEnabled);
  void RegisterOtaUpdateManager(GetFirmwareBootTypeFn getFirmwareBootType, TryStartFirmwareUpdateFn tryStartFirmwareUpdate);

  bool CaptivePortalBroadcastMessageBIN(std::span<const uint8_t> data);
  void CaptivePortalSetAlwaysEnabled(bool alwaysEnabled);

  FirmwareBootType OtaGetFirmwareBootType();
  bool OtaTryStartFirmwareUpdate(const OpenShock::SemVer& version);
}  // namespace OpenShock::AppHooks
