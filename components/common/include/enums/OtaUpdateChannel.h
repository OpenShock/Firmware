#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <string_view>

namespace OpenShock {
  enum class OtaUpdateChannel : uint8_t {
    Stable,
    Beta,
    Develop
  };

  inline bool TryParseOtaUpdateChannel(OtaUpdateChannel& channel, std::string_view str)
  {
    if (StringIEquals(str, "stable")) {
      channel = OtaUpdateChannel::Stable;
      return true;
    }

    if (StringIEquals(str, "beta")) {
      channel = OtaUpdateChannel::Beta;
      return true;
    }

    if (StringIEquals(str, "develop") || StringIEquals(str, "dev")) {
      channel = OtaUpdateChannel::Develop;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
