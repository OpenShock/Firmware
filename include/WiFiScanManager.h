#pragma once

#include <esp_wifi_types.h>

#include <functional>
#include <cstdint>

namespace OpenShock::WiFiScanManager {
  bool Init();

  bool IsScanning();

  bool StartScan();
  void CancelScan();

  typedef std::uint64_t CallbackHandle;

  typedef std::function<void()> ScanStartedHandler;
  CallbackHandle RegisterScanStartedHandler(const ScanStartedHandler& handler);
  void UnregisterScanStartedHandler(CallbackHandle id);

  enum class ScanCompletedStatus {
    Success,
    Cancelled,
    Error,
  };
  typedef std::function<void(ScanCompletedStatus)> ScanCompletedHandler;
  CallbackHandle RegisterScanCompletedHandler(const ScanCompletedHandler& handler);
  void UnregisterScanCompletedHandler(CallbackHandle id);

  typedef std::function<void(const wifi_ap_record_t* record)> ScanDiscoveryHandler;
  CallbackHandle RegisterScanDiscoveryHandler(const ScanDiscoveryHandler& handler);
  void UnregisterScanDiscoveryHandler(CallbackHandle id);

  void Update();
} // namespace OpenShock::WiFiScanManager
