#pragma once

namespace OpenShock {
  enum class ScanCompletedStatus {
    Completed,
    Cancelled,
    Error,
  };

  const char* GetScanCompletedStatusName(ScanCompletedStatus status);
}  // namespace OpenShock
