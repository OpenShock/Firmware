#pragma once

#include "wifi/WiFiScanStatus.h"

#include <esp_wifi_types.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace OpenShock::WiFiScanManager {
  // Called on the event loop when scan work is pending; must only wake the worker (e.g. a task notification)
  using WakeWorker = void (*)();

  /// @brief Scan events only mark work as pending and call wakeWorker; the worker task then calls ProcessEvents(),
  /// which fetches the results and runs the handlers there instead of on the system event task.
  [[nodiscard]] bool Init(WakeWorker wakeWorker);

  /// @brief Runs the handlers for scan events since the last call. Call from a single worker task.
  void ProcessEvents();

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
