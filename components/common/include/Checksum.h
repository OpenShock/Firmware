#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace OpenShock::Checksum {
  namespace Detail {
    // A value the object overloads below must NOT take: anything that already is (or converts to) a byte span, e.g.
    // std::span<uint8_t>, std::array<uint8_t, N> or uint8_t[N]. Those go to the span overloads, so a span is
    // checksummed over the bytes it points at, never over its own pointer/size representation.
    template<typename T>
    concept ChecksumObject = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && !std::is_convertible_v<const T&, std::span<const uint8_t>>;
  }  // namespace Detail

  // ------------------------------------------------------------
  // Raw byte span checksum
  // ------------------------------------------------------------
  constexpr uint8_t Sum8(std::span<const uint8_t> data)
  {
    uint8_t checksum = 0;
    for (uint8_t byte : data) {
      checksum += byte;
    }
    return checksum;
  }

  // ------------------------------------------------------------
  // Integer and trivially copyable object overload
  // ------------------------------------------------------------
  // A byte sum does not depend on byte order, so summing the object representation gives the same result for an
  // integer as summing its bytes by shifting. std::bit_cast keeps this usable in constant expressions
  // (reinterpret_cast is not). Constant evaluation still rejects types containing pointers, unions or padding,
  // which have no fixed byte representation.
  template<typename T>
    requires Detail::ChecksumObject<T>
  constexpr uint8_t Sum8(const T& value)
  {
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
    return Sum8(std::span<const uint8_t>(bytes));
  }

  // ------------------------------------------------------------
  // CRC-32 (IEEE 802.3): reflected, polynomial 0xEDB88320, init and final xor 0xFFFFFFFF.
  // The same CRC zlib.crc32() and esp_rom_crc32_le(0, ...) compute; kept bitwise and constexpr so it works in
  // constant expressions (the board descriptor placeholder is sealed at compile time) and has no table to carry.
  // ------------------------------------------------------------
  constexpr uint32_t Crc32(std::span<const uint8_t> data, uint32_t crc = 0)
  {
    crc = ~crc;
    for (uint8_t byte : data) {
      crc ^= byte;
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
      }
    }
    return ~crc;
  }

  template<typename T>
    requires(!std::integral<T> && Detail::ChecksumObject<T>)
  constexpr uint32_t Crc32(const T& data)
  {
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(data);
    return Crc32(std::span<const uint8_t>(bytes));
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
