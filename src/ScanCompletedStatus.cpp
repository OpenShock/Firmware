#include "ScanCompletedStatus.h"

const char* OpenShock::GetScanCompletedStatusName(OpenShock::ScanCompletedStatus status) {
  switch (status) {
    case ScanCompletedStatus::Completed:
      return "completed";
    case ScanCompletedStatus::Cancelled:
      return "cancelled";
    case ScanCompletedStatus::Error:
      return "error";
    default:
      return "unknown";
  }
}
