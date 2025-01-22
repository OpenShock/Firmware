#pragma once

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace OpenShock::Rmt::Internal {
  template<size_t N, typename T>
  constexpr void EncodeBits(rmt_data_t* sequence, T data, rmt_data_t rmtOne, rmt_data_t rmtZero)
  {
    static_assert(std::is_unsigned<T>::value, "T must be an unsigned integer");
    static_assert(N > 0, "N must be greater than 0");
    static_assert(N < std::numeric_limits<T>::digits, "N must be less or equal to the number of bits in T");

    for (size_t i = 0; i < N; ++i) {
      size_t bit_pos = N - (i + 1);
      sequence[i]    = (data >> bit_pos) & 1 ? rmtOne : rmtZero;
    }
  }
}  // namespace OpenShock::Rmt::Internal
