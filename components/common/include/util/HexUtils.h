#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace OpenShock::HexUtils {
  /// @brief Converts a single byte to a hex pair, and writes it to the output buffer.
  /// @param data The byte to convert.
  /// @param output The output buffer to write to.
  /// @param upper Whether to use uppercase hex characters.
  constexpr void ToHex(uint8_t data, char* output, bool upper = true) noexcept
  {
    const char* hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    output[0]       = hex[data >> 4];
    output[1]       = hex[data & 0x0F];
  }

  /// @brief Converts a byte array to a hex string.
  /// @param data The byte array to convert.
  /// @param output The output buffer to write to.
  /// @param upper Whether to use uppercase hex characters.
  /// @remark To use this you must specify the size of the array in the template parameter. (e.g. ToHexMac<6>(...))
  template<std::size_t N>
  constexpr void ToHex(const uint8_t (&data)[N], char (&output)[(N * 2) + 1], bool upper = true) noexcept
  {
    for (std::size_t i = 0; i < N; ++i) {
      ToHex(data[i], &output[i * 2], upper);
    }
    output[N * 2] = '\0';
  }

  /// @brief Converts a byte array to a hex string.
  /// @param data The byte array to convert.
  /// @param output The output buffer to write to.
  /// @param upper Whether to use uppercase hex characters.
  /// @remark To use this you must specify the size of the array in the template parameter. (e.g. ToHexMac<6>(...))
  template<std::size_t N>
  constexpr void ToHex(const uint8_t (&data)[N], std::array<char, (N * 2) + 1>& output, bool upper = true) noexcept
  {
    for (std::size_t i = 0; i < N; ++i) {
      ToHex(data[i], &output[i * 2], upper);
    }
    output[N * 2] = '\0';
  }

  /// @brief Converts a byte array to a hex string.
  /// @param data The byte array to convert.
  /// @param upper Whether to use uppercase hex characters.
  /// @return The hex string.
  /// @remark To use this you must specify the size of the array in the template parameter. (e.g. ToHexMac<6>(...))
  template<std::size_t N>
  constexpr std::array<char, (N * 2) + 1> ToHex(const uint8_t (&data)[N], bool upper = true) noexcept
  {
    std::array<char, (N * 2) + 1> output {};
    ToHex<N>(data, output, upper);
    return output;
  }

  /// @brief Converts a byte array to a MAC address string. (hex pairs separated by colons)
  /// @param data The byte array to convert.
  /// @param output The output buffer to write to.
  /// @param upper Whether to use uppercase hex characters.
  /// @remark To use this you must specify the size of the array in the template parameter. (e.g. ToHexMac<6>(...))
  template<std::size_t N>
  constexpr void ToHexMac(const uint8_t (&data)[N], std::array<char, N * 3>& output, bool upper = true) noexcept
  {
    const std::size_t Last = N - 1;
    for (std::size_t i = 0; i < Last; ++i) {
      ToHex(data[i], &output[i * 3], upper);
      output[i * 3 + 2] = ':';
    }
    ToHex(data[Last], &output[Last * 3], upper);
    output[(N * 3) - 1] = '\0';
  }

  /// @brief Converts a byte array to a MAC address string. (hex pairs separated by colons)
  /// @param data The byte array to convert.
  /// @param upper Whether to use uppercase hex characters.
  /// @return The hex string in a MAC address format.
  /// @remark To use this you must specify the size of the array in the template parameter. (e.g. ToHexMac<6>(...))
  template<std::size_t N>
  constexpr std::array<char, N * 3> ToHexMac(const uint8_t (&data)[N], bool upper = true) noexcept
  {
    std::array<char, N * 3> output {};
    ToHexMac<N>(data, output, upper);
    return output;
  }

  /// @brief Converts a hex pair to a byte.
  /// @param high The high nibble.
  /// @param low The low nibble.
  /// @return The byte, or std::nullopt if either character is not a hex digit.
  [[nodiscard]] constexpr std::optional<uint8_t> TryParseHexPair(char high, char low) noexcept
  {
    uint8_t output = 0;

    if (high >= '0' && high <= '9') {
      output = (high - '0') << 4;
    } else if (high >= 'A' && high <= 'F') {
      output = (high - 'A' + 10) << 4;
    } else if (high >= 'a' && high <= 'f') {
      output = (high - 'a' + 10) << 4;
    } else {
      return std::nullopt;
    }

    if (low >= '0' && low <= '9') {
      output |= low - '0';
    } else if (low >= 'A' && low <= 'F') {
      output |= low - 'A' + 10;
    } else if (low >= 'a' && low <= 'f') {
      output |= low - 'a' + 10;
    } else {
      return std::nullopt;
    }

    return output;
  }

  /// @brief Parses a MAC-style hex string (hex pairs separated by colons, e.g. "DE:AD:BE:EF:01:23") into `out`.
  /// @return The number of bytes parsed, or 0 on failure (bad length, bad digit/separator, or `out` too small).
  constexpr std::size_t TryParseHexMac(std::string_view str, std::span<uint8_t> out) noexcept
  {
    std::size_t parsedLength = (str.length() + 1) / 3;

    if ((parsedLength * 3) - 1 != str.length()) {
      return 0;  // Invalid MAC-Style hex string length.
    }

    if (parsedLength > out.size()) {
      return 0;  // Output buffer is too small.
    }

    for (std::size_t i = 0; i < parsedLength; ++i) {
      auto byte = TryParseHexPair(str[i * 3], str[i * 3 + 1]);
      if (!byte) {
        return 0;  // Invalid hex pair.
      }
      if (i + 1 < parsedLength && str[i * 3 + 2] != ':') {
        return 0;  // Invalid separator.
      }
      out[i] = *byte;
    }

    return parsedLength;
  }

  /// @brief Parses a plain hex string (e.g. "DEADBEEF") into `out`.
  /// @return The number of bytes parsed, or 0 on failure (odd length, bad digit, or `out` too small).
  constexpr std::size_t TryParseHex(std::string_view str, std::span<uint8_t> out) noexcept
  {
    std::size_t parsedLength = str.length() / 2;

    if (parsedLength * 2 != str.length()) {
      return 0;  // Invalid hex string length.
    }

    if (parsedLength > out.size()) {
      return 0;  // Output buffer is too small.
    }

    for (std::size_t i = 0; i < parsedLength; ++i) {
      auto byte = TryParseHexPair(str[i * 2], str[i * 2 + 1]);
      if (!byte) {
        return 0;  // Invalid hex pair.
      }
      out[i] = *byte;
    }

    return parsedLength;
  }
}  // namespace OpenShock::HexUtils
