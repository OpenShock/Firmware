#pragma once

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace OpenShock::Rmt::Internal {
  template<std::size_t N, typename T>
  inline void EncodeBits(std::vector<rmt_data_t>& pulses, T data, const rmt_data_t& rmtOne, const rmt_data_t& rmtZero) {
    static_assert(std::is_unsigned<T>::value, "T must be an unsigned integer");
    static_assert(N > 0, "N must be greater than 0");
    static_assert(N < std::numeric_limits<T>::digits, "N must be less or equal to the number of bits in T");

    pulses.reserve(pulses.size() + N);
    for (std::int64_t bit_pos = N - 1; bit_pos >= 0; --bit_pos) {
      pulses.push_back((data >> bit_pos) & 1 ? rmtOne : rmtZero);
    }
  }
}
