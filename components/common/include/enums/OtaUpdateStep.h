#pragma once

#include "StringHelpers.h"

#include <cstdint>
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

  inline bool TryParseOtaUpdateStep(OtaUpdateStep& channel, std::string_view str)
  {
    if (StringIEquals(str, "none")) {
      channel = OtaUpdateStep::None;
      return true;
    }

    if (StringIEquals(str, "updating")) {
      channel = OtaUpdateStep::Updating;
      return true;
    }

    if (StringIEquals(str, "updated")) {
      channel = OtaUpdateStep::Updated;
      return true;
    }

    if (StringIEquals(str, "validating")) {
      channel = OtaUpdateStep::Validating;
      return true;
    }

    if (StringIEquals(str, "validated")) {
      channel = OtaUpdateStep::Validated;
      return true;
    }

    if (StringIEquals(str, "rollingback")) {
      channel = OtaUpdateStep::RollingBack;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
