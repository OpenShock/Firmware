#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  enum class OtaUpdateChannel : uint8_t {
    Stable,
    Beta,
    Develop
  };

  [[nodiscard]] inline std::optional<OtaUpdateChannel> TryParseOtaUpdateChannel(std::string_view str)
  {
    if (StringIEquals(str, "stable")) {
      return OtaUpdateChannel::Stable;
    }

    if (StringIEquals(str, "beta")) {
      return OtaUpdateChannel::Beta;
    }

    if (StringIEquals(str, "develop") || StringIEquals(str, "dev")) {
      return OtaUpdateChannel::Develop;
    }

    return std::nullopt;
  }
}  // namespace OpenShock
