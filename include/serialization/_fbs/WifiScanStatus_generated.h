// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_WIFISCANSTATUS_OPENSHOCK_SERIALIZATION_TYPES_H_
#define FLATBUFFERS_GENERATED_WIFISCANSTATUS_OPENSHOCK_SERIALIZATION_TYPES_H_

#include "flatbuffers/flatbuffers.h"

// Ensure the included flatbuffers.h is the same version as when this file was
// generated, otherwise it may not be compatible.
static_assert(FLATBUFFERS_VERSION_MAJOR == 24 &&
              FLATBUFFERS_VERSION_MINOR == 3 &&
              FLATBUFFERS_VERSION_REVISION == 25,
             "Non-compatible flatbuffers version included");

namespace OpenShock {
namespace Serialization {
namespace Types {

enum class WifiScanStatus : uint8_t {
  Started = 0,
  InProgress = 1,
  Completed = 2,
  TimedOut = 3,
  Aborted = 4,
  Error = 5,
  MIN = Started,
  MAX = Error
};

inline const WifiScanStatus (&EnumValuesWifiScanStatus())[6] {
  static const WifiScanStatus values[] = {
    WifiScanStatus::Started,
    WifiScanStatus::InProgress,
    WifiScanStatus::Completed,
    WifiScanStatus::TimedOut,
    WifiScanStatus::Aborted,
    WifiScanStatus::Error
  };
  return values;
}

inline const char * const *EnumNamesWifiScanStatus() {
  static const char * const names[7] = {
    "Started",
    "InProgress",
    "Completed",
    "TimedOut",
    "Aborted",
    "Error",
    nullptr
  };
  return names;
}

inline const char *EnumNameWifiScanStatus(WifiScanStatus e) {
  if (::flatbuffers::IsOutRange(e, WifiScanStatus::Started, WifiScanStatus::Error)) return "";
  const size_t index = static_cast<size_t>(e);
  return EnumNamesWifiScanStatus()[index];
}

}  // namespace Types
}  // namespace Serialization
}  // namespace OpenShock

#endif  // FLATBUFFERS_GENERATED_WIFISCANSTATUS_OPENSHOCK_SERIALIZATION_TYPES_H_
