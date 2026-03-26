#pragma once

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace OpenShock::Rmt::Internal {
  template<size_t N, typename T>
  constexpr void EncodeBits(rmt_data_t* sequence, T data, const rmt_data_t& rmtOne, const rmt_data_t& rmtZero)
  {
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned integer");
    static_assert(N > 0, "N must be greater than 0");

    constexpr std::size_t BitCount = std::numeric_limits<T>::digits;
    static_assert(N <= BitCount, "N must be less or equal to the number of bits in T");

    // Align MSB to the top bit we care about
    data <<= (BitCount - N);

    constexpr T MsbMask = T(1) << (BitCount - 1);

    for (std::size_t i = 0; i < N; ++i) {
      sequence[i] = (data & MsbMask) ? rmtOne : rmtZero;
      data <<= 1;
    }
  }
}  // namespace OpenShock::Rmt::Internal
