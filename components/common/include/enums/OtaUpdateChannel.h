#pragma once

#include <cstdint>
#include <cstring>

namespace OpenShock {
  enum class OtaUpdateChannel : uint8_t {
    Stable,
    Beta,
    Develop
  };

  inline bool TryParseOtaUpdateChannel(OtaUpdateChannel& channel, const char* str)
  {
    if (strcasecmp(str, "stable") == 0) {
      channel = OtaUpdateChannel::Stable;
      return true;
    }

    if (strcasecmp(str, "beta") == 0) {
      channel = OtaUpdateChannel::Beta;
      return true;
    }

    if (strcasecmp(str, "develop") == 0 || strcasecmp(str, "dev") == 0) {
      channel = OtaUpdateChannel::Develop;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
