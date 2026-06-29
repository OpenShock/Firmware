#pragma once

#include "serialization/_fbs/HubConfig_generated.h"

#include <cstdint>
#include <cstring>

namespace OpenShock {
  typedef OpenShock::Serialization::Configuration::OtaUpdateStep OtaUpdateStep;

  inline bool TryParseOtaUpdateStep(OtaUpdateStep& channel, const char* str)
  {
    if (strcasecmp(str, "none") == 0) {
      channel = OtaUpdateStep::None;
      return true;
    }

    if (strcasecmp(str, "updating") == 0) {
      channel = OtaUpdateStep::Updating;
      return true;
    }

    if (strcasecmp(str, "updated") == 0) {
      channel = OtaUpdateStep::Updated;
      return true;
    }

    if (strcasecmp(str, "validating") == 0) {
      channel = OtaUpdateStep::Validating;
      return true;
    }

    if (strcasecmp(str, "validated") == 0) {
      channel = OtaUpdateStep::Validated;
      return true;
    }

    if (strcasecmp(str, "rollingback") == 0) {
      channel = OtaUpdateStep::RollingBack;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
