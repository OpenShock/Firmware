#pragma once

#include "enums/OtaUpdateStep.h"
#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  enum class FirmwareBootType : uint8_t {
    Normal,
    NewFirmware,
    Rollback
  };

  [[nodiscard]] inline std::optional<FirmwareBootType> TryParseFirmwareBootType(std::string_view str)
  {
    if (StringIEquals(str, "normal")) {
      return FirmwareBootType::Normal;
    }

    if (StringIEquals(str, "newfirmware") || StringIEquals(str, "new_firmware")) {
      return FirmwareBootType::NewFirmware;
    }

    if (StringIEquals(str, "rollback")) {
      return FirmwareBootType::Rollback;
    }

    return std::nullopt;
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
