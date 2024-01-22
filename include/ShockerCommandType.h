#pragma once

#include "serialization/_fbs/ShockerCommandType_generated.h"

#include <cstdint>

namespace OpenShock {
  typedef OpenShock::Serialization::Types::ShockerCommandType ShockerCommandType;

  inline bool ShockerCommandTypeFromString(const char* str, ShockerCommandType& out) {
    if (strcasecmp(str, "stop") == 0) {
      out = ShockerCommandType::Stop;
      return true;
    } else if (strcasecmp(str, "shock") == 0) {
      out = ShockerCommandType::Shock;
      return true;
    } else if (strcasecmp(str, "vibrate") == 0) {
      out = ShockerCommandType::Vibrate;
      return true;
    } else if (strcasecmp(str, "sound") == 0) {
      out = ShockerCommandType::Sound;
      return true;
    } else {
      return false;
    }
  }
}  // namespace OpenShock
