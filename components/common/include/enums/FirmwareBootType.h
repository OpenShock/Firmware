#pragma once

#include "StringHelpers.h"
#include "enums/OtaUpdateStep.h"

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

  // Maps the OTA update step persisted in config (read once at boot) to the boot
  // type this startup represents.
  constexpr FirmwareBootType InferFirmwareBootType(OtaUpdateStep step)
  {
    switch (step) {
      case OtaUpdateStep::Updated:
        return FirmwareBootType::NewFirmware;
      // Validating means we crashed mid-validation of the new firmware, so this
      // boot is the rollback to the previous image.
      case OtaUpdateStep::Validating:
      case OtaUpdateStep::RollingBack:
        return FirmwareBootType::Rollback;
      default:
        return FirmwareBootType::Normal;
    }
  }
}  // namespace OpenShock
