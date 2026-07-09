#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <string_view>

namespace OpenShock {
  enum class ShockerModelType : uint8_t {
    CaiXianlin,
    Petrainer,
    Petrainer998DR,
    WellturnT330,
    D80
  };

  inline bool TryParseShockerModelType(ShockerModelType& out, std::string_view str, bool allowTypo = false)
  {
    if (StringIEquals(str, "caixianlin") || StringIEquals(str, "cai-xianlin")) {
      out = ShockerModelType::CaiXianlin;
      return true;
    }

    if (StringIEquals(str, "petrainer")) {
      out = ShockerModelType::Petrainer;
      return true;
    }

    if (allowTypo && StringIEquals(str, "pettrainer")) {
      out = ShockerModelType::Petrainer;
      return true;
    }

    if (StringIEquals(str, "petrainer998dr")) {
      out = ShockerModelType::Petrainer998DR;
      return true;
    }

    if (allowTypo && StringIEquals(str, "pettrainer998dr")) {
      out = ShockerModelType::Petrainer998DR;
      return true;
    }

    if (StringIEquals(str, "wellturnt330") || StringIEquals(str, "t330")) {
      out = ShockerModelType::WellturnT330;
      return true;
    }

    if (StringIEquals(str, "d80")) {
      out = ShockerModelType::D80;
      return true;
    }

    return false;
  }
}  // namespace OpenShock
