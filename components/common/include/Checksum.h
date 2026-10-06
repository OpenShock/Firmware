#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

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
    // Shift on the unsigned representation; shifting signed values is implementation-defined for negatives
    std::make_unsigned_t<T> bits = static_cast<std::make_unsigned_t<T>>(value);

    uint8_t result = 0;

    for (std::size_t i = 0; i < sizeof(T); ++i) {
      result += static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
    }

    return result;
  }

  // ------------------------------------------------------------
  // Generic trivially copyable object overload
  // ------------------------------------------------------------
  // Not constexpr: it reads the object representation through reinterpret_cast.
  template<typename T>
    requires(!std::integral<T> && std::is_trivially_copyable_v<T>)
  inline uint8_t Sum8(const T& data)
  {
    static_assert(std::is_trivially_copyable_v<T>, "Sum8 only supports trivially copyable types");

    return Sum8(reinterpret_cast<const uint8_t*>(std::addressof(data)), sizeof(T));
  }

  /// @brief Reverses the bit order of the low nibble of `b` (lookup table packed into a 64-bit constant).
  ///        Only the low nibble is used, so the shift can never reach 64 bits.
  constexpr uint8_t ReverseNibble(uint8_t b)
  {
    return (0xF7B3D591E6A2C480ull >> ((b & 0xF) * 4)) & 0xF;
  }

  /// @brief Reverses and inverts the bits of the low nibble of `b`. Only the low nibble is used.
  constexpr uint8_t ReverseInverseNibble(uint8_t b)
  {
    return (0x084C2A6E195D3B7Full >> ((b & 0xF) * 4)) & 0xF;
  }
}  // namespace OpenShock::Checksum
