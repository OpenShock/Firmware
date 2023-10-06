#pragma once

#include "ScanCompletedStatus.h"

#include <esp_wifi_types.h>

#include <cstdint>
#include <functional>

namespace OpenShock::WiFiScanManager {
  bool Init();

  bool IsScanning();

  bool StartScan();
  void CancelScan();

  typedef std::function<void()> ScanStartedHandler;
  typedef std::function<void(OpenShock::ScanCompletedStatus)> ScanCompletedHandler;
  typedef std::function<void(const wifi_ap_record_t* record)> ScanDiscoveryHandler;

  std::uint64_t RegisterScanStartedHandler(const ScanStartedHandler& handler);
  void UnregisterScanStartedHandler(std::uint64_t id);

  std::uint64_t RegisterScanCompletedHandler(const ScanCompletedHandler& handler);
  void UnregisterScanCompletedHandler(std::uint64_t id);

  std::uint64_t RegisterScanDiscoveryHandler(const ScanDiscoveryHandler& handler);
  void UnregisterScanDiscoveryHandler(std::uint64_t id);

  void Update();
}  // namespace OpenShock::WiFiScanManager
