#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  enum class OtaUpdateStep : uint8_t {
    None,
    Updating,
    Updated,
    Validating,
    Validated,
    RollingBack
  };

  // The name jsonconfig renders; TryParseOtaUpdateStep reads it back.
  [[nodiscard]] constexpr const char* OtaUpdateStepName(OtaUpdateStep step)
  {
    switch (step) {
      case OtaUpdateStep::None:
        return "None";
      case OtaUpdateStep::Updating:
        return "Updating";
      case OtaUpdateStep::Updated:
        return "Updated";
      case OtaUpdateStep::Validating:
        return "Validating";
      case OtaUpdateStep::Validated:
        return "Validated";
      case OtaUpdateStep::RollingBack:
        return "RollingBack";
    }
    return "";
  }

  [[nodiscard]] inline std::optional<OtaUpdateStep> TryParseOtaUpdateStep(std::string_view str)
  {
    if (StringIEquals(str, "none")) {
      return OtaUpdateStep::None;
    }

    if (StringIEquals(str, "updating")) {
      return OtaUpdateStep::Updating;
    }

    if (StringIEquals(str, "updated")) {
      return OtaUpdateStep::Updated;
    }

    if (StringIEquals(str, "validating")) {
      return OtaUpdateStep::Validating;
    }

    if (StringIEquals(str, "validated")) {
      return OtaUpdateStep::Validated;
    }

    if (StringIEquals(str, "rollingback")) {
      return OtaUpdateStep::RollingBack;
    }

    return std::nullopt;
  }
}  // namespace OpenShock
