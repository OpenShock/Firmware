#include "AppHooks.h"

#include <atomic>

using namespace OpenShock;

static std::atomic<AppHooks::BroadcastMessageBINFn> s_broadcastMessageBIN       = nullptr;
static std::atomic<AppHooks::SetAlwaysEnabledFn> s_setAlwaysEnabled             = nullptr;
static std::atomic<AppHooks::GetFirmwareBootTypeFn> s_getFirmwareBootType       = nullptr;
static std::atomic<AppHooks::TryStartFirmwareUpdateFn> s_tryStartFirmwareUpdate = nullptr;
static std::atomic<AppHooks::RequestUpdateCheckFn> s_requestUpdateCheck         = nullptr;

void AppHooks::RegisterCaptivePortal(BroadcastMessageBINFn broadcastMessageBIN, SetAlwaysEnabledFn setAlwaysEnabled)
{
  s_broadcastMessageBIN = broadcastMessageBIN;
  s_setAlwaysEnabled    = setAlwaysEnabled;
}

void AppHooks::RegisterOtaUpdateManager(GetFirmwareBootTypeFn getFirmwareBootType, TryStartFirmwareUpdateFn tryStartFirmwareUpdate, RequestUpdateCheckFn requestUpdateCheck)
{
  s_getFirmwareBootType    = getFirmwareBootType;
  s_tryStartFirmwareUpdate = tryStartFirmwareUpdate;
  s_requestUpdateCheck     = requestUpdateCheck;
}

bool AppHooks::CaptivePortalBroadcastMessageBIN(std::span<const uint8_t> data)
{
  auto fn = s_broadcastMessageBIN.load();
  return fn != nullptr && fn(data);
}

void AppHooks::CaptivePortalSetAlwaysEnabled(bool alwaysEnabled)
{
  auto fn = s_setAlwaysEnabled.load();
  if (fn != nullptr) fn(alwaysEnabled);
}

FirmwareBootType AppHooks::OtaGetFirmwareBootType()
{
  auto fn = s_getFirmwareBootType.load();
  return fn != nullptr ? fn() : FirmwareBootType::Normal;
}

bool AppHooks::OtaTryStartFirmwareUpdate(const OpenShock::SemVer& version)
{
  auto fn = s_tryStartFirmwareUpdate.load();
  return fn != nullptr && fn(version);
}

bool AppHooks::OtaRequestUpdateCheck()
{
  auto fn = s_requestUpdateCheck.load();
  return fn != nullptr && fn();
}
