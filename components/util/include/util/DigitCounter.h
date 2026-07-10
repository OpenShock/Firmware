#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace OpenShock::Util {
  // Maximum number of base-10 characters T can render to: its digit count plus a '-' for signed types.
  template<typename T>
  inline constexpr int Digits10CountMax = std::numeric_limits<T>::digits10 + (std::is_signed_v<T> ? 2 : 1);

  // Number of base-10 characters val renders to, counting a leading '-' when negative.
  template<std::integral T>
  constexpr std::size_t Digits10Count(T val)
  {
    std::size_t count = 1;
    std::make_unsigned_t<T> mag;

    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        ++count;  // the leading '-'
        mag = std::make_unsigned_t<T>(0) - static_cast<std::make_unsigned_t<T>>(val);  // magnitude via modular negation
      } else {
        mag = static_cast<std::make_unsigned_t<T>>(val);
      }
    } else {
      mag = val;
    }

    while (mag >= 10) {
      mag /= 10;
      ++count;
    }

    return count;
  }
}  // namespace OpenShock::Util
