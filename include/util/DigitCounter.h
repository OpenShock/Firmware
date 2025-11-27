#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace OpenShock::Util {
  template<typename T>
  inline constexpr int Digits10CountMax = std::numeric_limits<T>::digits10 + (std::is_signed_v<T> ? 2 : 1);

  template<typename T>
  constexpr std::size_t Digits10Count(T val)
  {
    static_assert(std::is_integral_v<T>);

    std::size_t digits = 1;

    if constexpr (std::is_signed_v<T> && val < 0) {
      digits++;
      val = -val;
    }

    while (val >= 10) {
      val /= 10;
      digits++;
    }

    return digits;
  }
}  // namespace OpenShock::Util
