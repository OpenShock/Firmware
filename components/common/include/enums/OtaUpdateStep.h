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
