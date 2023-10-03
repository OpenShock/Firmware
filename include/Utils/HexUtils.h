#pragma once

#include <nonstd/span.hpp>

#include <array>
#include <cstdint>

namespace OpenShock::HexUtils {
  constexpr void ToHex(std::uint8_t data, char* output, bool upper = true) noexcept {
    const char* hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    output[0]       = hex[data >> 4];
    output[1]       = hex[data & 0x0F];
  }

  template<std::size_t N>
  constexpr void ToHex(nonstd::span<const std::uint8_t, N> data, nonstd::span<char, N * 2> output, bool upper = true) noexcept {
    for (std::size_t i = 0; i < data.size(); ++i) {
      ToHex(data[i], &output[i * 2], upper);
    }
  }
  template<std::size_t N>
  constexpr std::array<char, (N * 2) + 1> ToHex(nonstd::span<const std::uint8_t, N> data, bool upper = true) noexcept {
    std::array<char, (N * 2) + 1> output {};
    ToHex(data, output, upper);
    output[N * 2] = '\0';
    return output;
  }

  template<std::size_t N>
  constexpr void ToHexMac(nonstd::span<const std::uint8_t, N> data, nonstd::span<char, (N * 3) - 1> output, bool upper = true) noexcept {
    const std::size_t Last = N - 1;
    for (std::size_t i = 0; i < Last; ++i) {
      ToHex(data[i], &output[i * 3], upper);
      output[i * 3 + 2] = ':';
    }
    ToHex(data[Last], &output[Last * 3], upper);
  }
  template<std::size_t N>
  constexpr std::array<char, N * 3> ToHexMac(nonstd::span<const std::uint8_t, N> data, bool upper = true) noexcept {
    std::array<char, N * 3> output {};
    ToHexMac(data, nonstd::span<char, (N * 3) - 1>(output.data(), output.size() - 1), upper);
    output[(N * 3) - 1] = '\0';
    return output;
  }
}  // namespace OpenShock::HexUtils
