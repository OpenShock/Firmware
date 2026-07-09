#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <string_view>

namespace OpenShock {
  enum class FirmwareBootType : uint8_t {
    Normal,
    NewFirmware,
    Rollback
  };

  inline bool TryParseFirmwareBootType(FirmwareBootType& bootType, std::string_view str)
  {
    if (StringIEquals(str, "normal")) {
      bootType = FirmwareBootType::Normal;
      return true;
    }

    if (StringIEquals(str, "newfirmware") || StringIEquals(str, "new_firmware")) {
      bootType = FirmwareBootType::NewFirmware;
      return true;
    }

    if (StringIEquals(str, "rollback")) {
      bootType = FirmwareBootType::Rollback;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
