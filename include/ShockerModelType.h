#pragma once

#include <cstdint>
#include <cstring>

namespace OpenShock {
  enum class ShockerModelType : uint8_t {
    CaiXianlin,
    Petrainer,
    Petrainer998DR,
    T330,
    D80
  };

  inline bool ShockerModelTypeFromString(const char* str, ShockerModelType& out, bool allowTypo = false)
  {
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

    if (strcasecmp(str, "petrainer998dr") == 0) {
      out = ShockerModelType::Petrainer998DR;
      return true;
    }

    if (allowTypo && strcasecmp(str, "pettrainer998dr") == 0) {
      out = ShockerModelType::Petrainer998DR;
      return true;
    }

    if (strcasecmp(str, "t330") == 0) {
      out = ShockerModelType::T330;
      return true;
    }

    if (strcasecmp(str, "d80") == 0) {
      out = ShockerModelType::D80;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
