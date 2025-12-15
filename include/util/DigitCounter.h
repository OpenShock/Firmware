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
    using Decayed = std::remove_cv_t<std::remove_reference_t<T>>;
    static_assert(std::is_integral_v<Decayed>);

    using U = std::make_unsigned_t<Decayed>;

    std::size_t digits = 1;
    U u;

    if constexpr (std::is_signed_v<Decayed>) {
      if (val < 0) {
        // Account for the sign
        digits++;

        // Safe magnitude via unsigned modular negation
        u = U(0) - static_cast<U>(val);
      } else {
        u = static_cast<U>(val);
      }
    } else {
      u = static_cast<U>(val);
    }

    // Now count digits of u (magnitude), base-10
    while (u >= 10) {
      u /= 10;
      digits++;
    }

    return digits;
  }
}  // namespace OpenShock::Util
