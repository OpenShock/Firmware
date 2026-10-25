#pragma once

#include "wifi/WiFiScanStatus.h"

#include <esp_wifi_types.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace OpenShock::WiFiScanManager {
  [[nodiscard]] bool Init();

  bool IsScanning();

  bool StartScan();
  bool AbortScan();

  // Stored in the registry until unregistered; moved in, never copied.
  using StatusChangedHandler      = std::move_only_function<void(OpenShock::WiFiScanStatus)>;
  using NetworksDiscoveredHandler = std::move_only_function<void(const std::vector<const wifi_ap_record_t*>& networkRecords)>;

  uint64_t RegisterStatusChangedHandler(StatusChangedHandler handler);
  void UnregisterStatusChangedHandler(uint64_t id);

  uint64_t RegisterNetworksDiscoveredHandler(NetworksDiscoveredHandler handler);
  void UnregisterNetworksDiscoveredHandler(uint64_t id);
}  // namespace OpenShock::WiFiScanManager
