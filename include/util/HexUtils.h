#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace OpenShock::HexUtils {
  /// @brief Converts a single byte to a hex pair, and writes it to the output buffer.
  /// @param data The byte to convert.
  /// @param output The output buffer to write to.
  /// @param upper Whether to use uppercase hex characters.
  constexpr void ToHex(std::uint8_t data, char* output, bool upper = true) noexcept {
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
  constexpr void ToHex(const std::uint8_t (&data)[N], char (&output)[(N * 2) + 1], bool upper = true) noexcept {
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
  constexpr std::array<char, (N * 2) + 1> ToHex(const std::uint8_t (&data)[N], bool upper = true) noexcept {
    std::array<char, (N * 2) + 1> output {};
    ToHex(data, output, upper);
    return output;
  }

  /// @brief Converts a byte array to a MAC address string. (hex pairs separated by colons)
  /// @param data The byte array to convert.
  /// @param output The output buffer to write to.
  /// @param upper Whether to use uppercase hex characters.
  /// @remark To use this you must specify the size of the array in the template parameter. (e.g. ToHexMac<6>(...))
  template<std::size_t N>
  constexpr void ToHexMac(const std::uint8_t (&data)[N], std::array<char, N * 3>& output, bool upper = true) noexcept {
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
  constexpr std::array<char, N * 3> ToHexMac(const std::uint8_t (&data)[N], bool upper = true) noexcept {
    std::array<char, N * 3> output {};
    ToHexMac<N>(data, output, upper);
    return output;
  }

  /// @brief Converts a hex pair to a byte.
  /// @param high The high nibble.
  /// @param low The low nibble.
  /// @param output The output buffer to write to.
  /// @return Whether the conversion was successful.
  constexpr bool TryParseHexPair(char high, char low, std::uint8_t& output) noexcept {
    if (high >= '0' && high <= '9') {
      output = (high - '0') << 4;
    } else if (high >= 'A' && high <= 'F') {
      output = (high - 'A' + 10) << 4;
    } else if (high >= 'a' && high <= 'f') {
      output = (high - 'a' + 10) << 4;
    } else {
      return false;
    }

    if (low >= '0' && low <= '9') {
      output |= low - '0';
    } else if (low >= 'A' && low <= 'F') {
      output |= low - 'A' + 10;
    } else if (low >= 'a' && low <= 'f') {
      output |= low - 'a' + 10;
    } else {
      return false;
    }

    return true;
  }

  constexpr std::size_t TryParseHexMac(const char* str, std::size_t strLen, std::uint8_t* out, std::size_t outLen) noexcept {
    std::size_t parsedLength = (strLen + 1) / 3;

    if ((parsedLength * 3) - 1 != strLen) {
      return 0;  // Invalid MAC-Style hex string length.
    }

    if (parsedLength > outLen) {
      return 0;  // Output buffer is too small.
    }

    for (std::size_t i = 0; i < parsedLength - 1; ++i) {
      if (!TryParseHexPair(str[i * 3], str[i * 3 + 1], out[i])) {
        return 0;  // Invalid hex pair.
      }
      if (str[i * 3 + 2] != ':') {
        return 0;  // Invalid separator.
      }
    }

    if (!TryParseHexPair(str[(parsedLength - 1) * 3], str[(parsedLength - 1) * 3 + 1], out[parsedLength - 1])) {
      return 0;  // Invalid hex pair.
    }

    return parsedLength;
  }

  inline std::size_t TryParseHexMac(const char* str, std::uint8_t* out, std::size_t outLen) noexcept {
    return TryParseHexMac(str, strlen(str), out, outLen);
  }
}  // namespace OpenShock::HexUtils
