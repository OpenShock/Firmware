#pragma once

#include "serialization/_fbs/FirmwareBootType_generated.h"

#include <cstring>
#include <cstdint>

namespace OpenShock {
  typedef OpenShock::Serialization::Types::FirmwareBootType FirmwareBootType;

  inline bool TryParseFirmwareBootType(FirmwareBootType& bootType, const char* str) {
    if (strcasecmp(str, "normal") == 0) {
      bootType = FirmwareBootType::Normal;
      return true;
    }

    if (strcasecmp(str, "new") == 0) {
      bootType = FirmwareBootType::New;
      return true;
    }

    if (strcasecmp(str, "rollback") == 0) {
      bootType = FirmwareBootType::Rollback;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
