#pragma once

#include <concepts>
#include <cstdint>
#include <limits>

namespace OpenShock::PercentUtils {
  /// @brief `Percent` (0-100) of an unsigned type's full scale, rounded to nearest. The percent is a template argument
  ///        so it can be checked with static_assert, and the function is consteval: the floating point math only ever
  ///        runs in the compiler.
  template<std::unsigned_integral T, double Percent>
  consteval T OfMax()
  {
    static_assert(Percent >= 0.0 && Percent <= 100.0, "a percent is 0-100");
    return static_cast<T>(Percent * static_cast<double>(std::numeric_limits<T>::max()) / 100.0 + 0.5);
  }

  static_assert(OfMax<uint16_t, 0.0>() == 0);
  static_assert(OfMax<uint16_t, 100.0>() == 65535);
  static_assert(OfMax<uint8_t, 50.0>() == 128);
}  // namespace OpenShock::PercentUtils
