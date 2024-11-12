#pragma once

namespace OpenShock::WiFiScanManager {
  [[nodiscard]] bool Init();

  bool IsScanning();

  bool StartScan();
  void AbortScan();

  bool FreeResources();
}  // namespace OpenShock::WiFiScanManager
