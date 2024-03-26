#pragma once

#include "serialization/_fbs/ShockerModelType_generated.h"

#include <cstdint>
#include <cstring>

namespace OpenShock {
  typedef OpenShock::Serialization::Types::ShockerModelType ShockerModelType;

  inline bool ShockerModelTypeFromString(const char* str, ShockerModelType& out, bool allowTypo = false) {
    if (strcasecmp(str, "caixianlin") == 0 || strcasecmp(str, "cai-xianlin") == 0) {
      out = ShockerModelType::CaiXianlin;
      return true;
    }

    if (strcasecmp(str, "petrainer") == 0) {
      out = ShockerModelType::Petrainer;
      return true;
    }

    if (allowTypo && strcasecmp(str, "pettrainer") == 0) {
      out = ShockerModelType::Petrainer;
      return true;
    }

    if (strcasecmp(str, "petrainertwo") == 0) {
      out = ShockerModelType::PetrainerTwo;
      return true;
    }

    if (allowTypo && strcasecmp(str, "pettrainertwo") == 0) {
      out = ShockerModelType::PetrainerTwo;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
