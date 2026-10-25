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

  // Moved in, never copied. Handlers can only be registered before Init(): the lists are then fixed, so they are
  // called without a lock and may call back into this manager or take their own locks.
  using StatusChangedHandler      = std::move_only_function<void(OpenShock::WiFiScanStatus)>;
  using NetworksDiscoveredHandler = std::move_only_function<void(const std::vector<const wifi_ap_record_t*>& networkRecords)>;

  bool RegisterStatusChangedHandler(StatusChangedHandler handler);
  bool RegisterNetworksDiscoveredHandler(NetworksDiscoveredHandler handler);
}  // namespace OpenShock::WiFiScanManager
