#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace OpenShock::Util {
  template<typename T>
  constexpr std::size_t Digits10CountMax()
  {
    static_assert(std::is_integral<T>::value);
    uint64_t num = std::numeric_limits<T>::max();

    std::size_t digits = std::is_signed<T>::value ? 2 : 1;
    while (num >= 10) {
      num /= 10;
      digits++;
    }

    return digits;
  }

  template<typename T>
  constexpr std::size_t Digits10Count(T val)
  {
    static_assert(std::is_integral<T>::value);

    std::size_t digits = 1;

    if (std::is_signed<T>::value && val < 0) {
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
