#pragma once

#include "enums/FirmwareBootType.h"
#include "SemVer.h"

#include <cstdint>
#include <span>

// core sits below the captive_portal and ota components, so it cannot call them directly.
// They register these callbacks from their Init(); until then each call behaves as if the
// subsystem were not running (broadcasts fail, requests are dropped).
namespace OpenShock::AppHooks {
  using BroadcastMessageBINFn    = bool (*)(std::span<const uint8_t> data);
  using SetAlwaysEnabledFn       = void (*)(bool alwaysEnabled);
  using IsRunningFn              = bool (*)();
  using GetFirmwareBootTypeFn    = FirmwareBootType (*)();
  using TryStartFirmwareUpdateFn = bool (*)(const OpenShock::SemVer& version);
  using RequestUpdateCheckFn     = bool (*)();

  void RegisterCaptivePortal(BroadcastMessageBINFn broadcastMessageBIN, SetAlwaysEnabledFn setAlwaysEnabled, IsRunningFn isRunning);
  void RegisterOtaUpdateManager(GetFirmwareBootTypeFn getFirmwareBootType, TryStartFirmwareUpdateFn tryStartFirmwareUpdate, RequestUpdateCheckFn requestUpdateCheck);

  bool CaptivePortalBroadcastMessageBIN(std::span<const uint8_t> data);
  void CaptivePortalSetAlwaysEnabled(bool alwaysEnabled);
  bool CaptivePortalIsRunning();

  FirmwareBootType OtaGetFirmwareBootType();
  bool OtaTryStartFirmwareUpdate(const OpenShock::SemVer& version);
  bool OtaRequestUpdateCheck();
}  // namespace OpenShock::AppHooks
