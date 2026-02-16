#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace OpenShock::Checksum {

  // ------------------------------------------------------------
  // Raw byte span checksum
  // ------------------------------------------------------------
  constexpr uint8_t Sum8(const uint8_t* data, std::size_t size)
  {
    uint8_t checksum = 0;
    for (std::size_t i = 0; i < size; ++i) {
      checksum += data[i];
    }
    return checksum;
  }

  // ------------------------------------------------------------
  // Generic integral overload (C++20 concepts)
  // ------------------------------------------------------------
  template<std::integral T>
  constexpr uint8_t Sum8(T value)
  {
    uint8_t result = 0;

    for (std::size_t i = 0; i < sizeof(T); ++i) {
      result += static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }

    return result;
  }

  // ------------------------------------------------------------
  // Generic trivially copyable object overload
  // ------------------------------------------------------------
  template<typename T>
    requires(!std::integral<T> && std::is_trivially_copyable_v<T>)
  constexpr uint8_t Sum8(const T& data)
  {
    return Sum8(reinterpret_cast<const uint8_t*>(std::addressof(data)), sizeof(T));
  }

  /**
   * Make sure the uint8 only has its high bits (0x0F) set before using this function
   */
  constexpr uint8_t ReverseNibble(uint8_t b)
  {
    return (0xF7B3D591E6A2C480ull >> (b * 4)) & 0xF;  // Trust me bro
  }

  /**
   * Make sure the uint8 only has its high bits (0x0F) set before using this function
   */
  constexpr uint8_t ReverseInverseNibble(uint8_t b)
  {
    return (0x084C2A6E195D3B7Full >> (b * 4)) & 0xF;  // Trust me bro
  }
}  // namespace OpenShock::Checksum

#undef SUM8_INT_FN
