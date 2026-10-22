#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
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
  // std::bit_cast keeps this usable in constant expressions (reinterpret_cast is not). Constant evaluation still
  // rejects types containing pointers, unions or padding, which have no fixed byte representation.
  template<typename T>
    requires(!std::integral<T> && std::is_trivially_copyable_v<T>)
  constexpr uint8_t Sum8(const T& data)
  {
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(data);
    return Sum8(bytes.data(), bytes.size());
  }

  // ------------------------------------------------------------
  // CRC-32 (IEEE 802.3): reflected, polynomial 0xEDB88320, init and final xor 0xFFFFFFFF.
  // The same CRC zlib.crc32() and esp_rom_crc32_le(0, ...) compute; kept bitwise and constexpr so it works in
  // constant expressions (the board descriptor placeholder is sealed at compile time) and has no table to carry.
  // ------------------------------------------------------------
  constexpr uint32_t Crc32(const uint8_t* data, std::size_t size, uint32_t crc = 0)
  {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
      crc ^= data[i];
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
      }
    }
    return ~crc;
  }

  template<typename T>
    requires(!std::integral<T> && std::is_trivially_copyable_v<T>)
  constexpr uint32_t Crc32(const T& data)
  {
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(data);
    return Crc32(bytes.data(), bytes.size());
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
