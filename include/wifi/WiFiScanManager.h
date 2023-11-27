#pragma once

#include "wifi/WiFiScanStatus.h"

#include <esp_wifi_types.h>

#include <cstdint>
#include <functional>

namespace OpenShock::WiFiScanManager {
  bool Init();

  bool IsScanning();

  bool StartScan();
  bool AbortScan();

  typedef std::function<void(OpenShock::WiFiScanStatus)> StatusChangedHandler;
  typedef std::function<void(const wifi_ap_record_t* record)> NetworkDiscoveryHandler;

  std::uint64_t RegisterStatusChangedHandler(const StatusChangedHandler& handler);
  void UnregisterStatusChangedHandler(std::uint64_t id);

  std::uint64_t RegisterNetworkDiscoveryHandler(const NetworkDiscoveryHandler& handler);
  void UnregisterNetworkDiscoveredHandler(std::uint64_t id);
}  // namespace OpenShock::WiFiScanManager
