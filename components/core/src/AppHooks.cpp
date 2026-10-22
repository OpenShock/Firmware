#include "AppHooks.h"

#include <atomic>

using namespace OpenShock;

namespace {
  struct CaptivePortalHooks {
    AppHooks::BroadcastMessageBINFn broadcastMessageBIN;
    AppHooks::SetAlwaysEnabledFn setAlwaysEnabled;
  };
  struct OtaUpdateManagerHooks {
    AppHooks::GetFirmwareBootTypeFn getFirmwareBootType;
    AppHooks::TryStartFirmwareUpdateFn tryStartFirmwareUpdate;
    AppHooks::RequestUpdateCheckFn requestUpdateCheck;
  };
}  // namespace

// Each subsystem registers once, from its Init(). The set is filled in first and then published through one pointer,
// so a caller on another task sees either no hooks or all of them.
static CaptivePortalHooks s_captivePortalHooks;
static OtaUpdateManagerHooks s_otaUpdateManagerHooks;
static std::atomic<const CaptivePortalHooks*> s_captivePortal       = nullptr;
static std::atomic<const OtaUpdateManagerHooks*> s_otaUpdateManager = nullptr;

void AppHooks::RegisterCaptivePortal(BroadcastMessageBINFn broadcastMessageBIN, SetAlwaysEnabledFn setAlwaysEnabled)
{
  s_captivePortalHooks = {broadcastMessageBIN, setAlwaysEnabled};
  s_captivePortal.store(&s_captivePortalHooks, std::memory_order_release);
}

void AppHooks::RegisterOtaUpdateManager(GetFirmwareBootTypeFn getFirmwareBootType, TryStartFirmwareUpdateFn tryStartFirmwareUpdate, RequestUpdateCheckFn requestUpdateCheck)
{
  s_otaUpdateManagerHooks = {getFirmwareBootType, tryStartFirmwareUpdate, requestUpdateCheck};
  s_otaUpdateManager.store(&s_otaUpdateManagerHooks, std::memory_order_release);
}

bool AppHooks::CaptivePortalBroadcastMessageBIN(std::span<const uint8_t> data)
{
  auto hooks = s_captivePortal.load(std::memory_order_acquire);
  return hooks != nullptr && hooks->broadcastMessageBIN != nullptr && hooks->broadcastMessageBIN(data);
}

void AppHooks::CaptivePortalSetAlwaysEnabled(bool alwaysEnabled)
{
  auto hooks = s_captivePortal.load(std::memory_order_acquire);
  if (hooks != nullptr && hooks->setAlwaysEnabled != nullptr) hooks->setAlwaysEnabled(alwaysEnabled);
}

FirmwareBootType AppHooks::OtaGetFirmwareBootType()
{
  auto hooks = s_otaUpdateManager.load(std::memory_order_acquire);
  return hooks != nullptr && hooks->getFirmwareBootType != nullptr ? hooks->getFirmwareBootType() : FirmwareBootType::Normal;
}

bool AppHooks::OtaTryStartFirmwareUpdate(const OpenShock::SemVer& version)
{
  auto hooks = s_otaUpdateManager.load(std::memory_order_acquire);
  return hooks != nullptr && hooks->tryStartFirmwareUpdate != nullptr && hooks->tryStartFirmwareUpdate(version);
}

bool AppHooks::OtaRequestUpdateCheck()
{
  auto hooks = s_otaUpdateManager.load(std::memory_order_acquire);
  return hooks != nullptr && hooks->requestUpdateCheck != nullptr && hooks->requestUpdateCheck();
}
