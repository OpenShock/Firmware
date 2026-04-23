#pragma once

#include <cstdint>
#include <cstring>

namespace OpenShock {
  enum class FirmwareBootType : uint8_t {
    Normal,
    NewFirmware,
    Rollback
  };

  inline bool TryParseFirmwareBootType(FirmwareBootType& bootType, const char* str)
  {
    if (strcasecmp(str, "normal") == 0) {
      bootType = FirmwareBootType::Normal;
      return true;
    }

    if (strcasecmp(str, "newfirmware") == 0 || strcasecmp(str, "new_firmware") == 0) {
      bootType = FirmwareBootType::NewFirmware;
      return true;
    }

    if (strcasecmp(str, "rollback") == 0) {
      bootType = FirmwareBootType::Rollback;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
