#pragma once

#include "fbs/WifiScanStatus_generated.h"

#include <esp_wifi_types.h>

#include <cstdint>
#include <functional>

namespace OpenShock::WiFiScanManager {
  bool Init();

  bool IsScanning();

  bool StartScan();
  void AbortScan();

  typedef std::function<void(OpenShock::WifiScanStatus)> StatusChangedHandler;
  typedef std::function<void(const wifi_ap_record_t* record)> NetworkDiscoveryHandler;

  std::uint64_t RegisterStatusChangedHandler(const StatusChangedHandler& handler);
  void UnregisterStatusChangedHandler(std::uint64_t id);

  std::uint64_t RegisterNetworkDiscoveryHandler(const NetworkDiscoveryHandler& handler);
  void UnregisterNetworkDiscoveryHandler(std::uint64_t id);

  void Update();
}  // namespace OpenShock::WiFiScanManager
