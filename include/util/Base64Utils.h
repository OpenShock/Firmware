#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace OpenShock::Base64Utils {
  /// @brief Calculates the size of the buffer required to hold the base64 encoded data.
  /// @param size The size of the data to encode.
  /// @return The size of the buffer required to hold the base64 encoded data.
  constexpr std::size_t CalculateEncodedSize(std::size_t size) noexcept {
    return (4 * (size / 3)) + 4; // TODO: This is wrong, but what mbedtls requires???
  }

  /// @brief Calculates the size of the buffer required to hold the decoded data.
  /// @param size The size of the data to decode.
  /// @return The size of the buffer required to hold the decoded data.
  constexpr std::size_t CalculateDecodedSize(std::size_t size) noexcept {
    return 3 * size / 4;
  }

  /// @brief Encodes a byte array to base64.
  /// @param data The data to encode.
  /// @param dataLen The size of the data to encode.
  /// @param output
  /// @param outputLen
  /// @return The amount of bytes written to the output buffer.
  std::size_t Encode(const std::uint8_t* data, std::size_t dataLen, char* output, std::size_t outputLen) noexcept;

  /// @brief Encodes a byte array to base64.
  /// @param data The data to encode.
  /// @param dataLen The size of the data to encode.
  /// @param output The output string to write to.
  /// @return The amount of bytes written to the output buffer.
  bool Encode(const std::uint8_t* data, std::size_t dataLen, std::string& output);

  /// @brief Decodes a base64 string.
  /// @param data The data to decode.
  /// @param dataLen The size of the data to decode.
  /// @param output The output buffer to write to.
  /// @param outputLen The size of the output buffer.
  /// @return The amount of bytes written to the output buffer.
  std::size_t Decode(const char* data, std::size_t dataLen, std::uint8_t* output, std::size_t outputLen) noexcept;

  /// @brief Decodes a base64 string.
  /// @param data The data to decode.
  /// @param dataLen The size of the data to decode.
  /// @param output The output buffer to write to.
  /// @return The amount of bytes written to the output buffer.
  bool Decode(const char* data, std::size_t dataLen, std::vector<std::uint8_t>& output) noexcept;
}  // namespace OpenShock::Base64Utils
