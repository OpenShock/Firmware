#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  enum class ShockerModelType : uint8_t {
    CaiXianlin,
    Petrainer,
    Petrainer998DR,
    WellturnT330,
    D80
  };

  [[nodiscard]] inline std::optional<ShockerModelType> TryParseShockerModelType(std::string_view str, bool allowTypo = false)
  {
    if (StringIEquals(str, "caixianlin") || StringIEquals(str, "cai-xianlin")) {
      return ShockerModelType::CaiXianlin;
    }

    if (StringIEquals(str, "petrainer")) {
      return ShockerModelType::Petrainer;
    }

    if (allowTypo && StringIEquals(str, "pettrainer")) {
      return ShockerModelType::Petrainer;
    }

    if (StringIEquals(str, "petrainer998dr")) {
      return ShockerModelType::Petrainer998DR;
    }

    if (allowTypo && StringIEquals(str, "pettrainer998dr")) {
      return ShockerModelType::Petrainer998DR;
    }

    if (StringIEquals(str, "wellturnt330") || StringIEquals(str, "t330")) {
      return ShockerModelType::WellturnT330;
    }

    if (StringIEquals(str, "d80")) {
      return ShockerModelType::D80;
    }

    return std::nullopt;
  }
}  // namespace OpenShock
