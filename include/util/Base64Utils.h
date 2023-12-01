#pragma once

#include <mbedtls/base64.h>

#include <cstdint>
#include <cstring>
#include <string>

// IMPORTANT: WE DONT USE ARRAYS OR TEMPLATES HERE

namespace OpenShock::Base64Utils {
  /// @brief Calculates the size of the buffer required to hold the base64 encoded data.
  /// @param size The size of the data to encode.
  /// @return The size of the buffer required to hold the base64 encoded data.
  constexpr std::size_t CalculateEncodedSize(std::size_t size) noexcept {
    return (size + 2) / 3 * 4;
  }

  /// @brief Calculates the size of the buffer required to hold the decoded data.
  /// @param size The size of the data to decode.
  /// @return The size of the buffer required to hold the decoded data.
  constexpr std::size_t CalculateDecodedSize(std::size_t size) noexcept {
    return (size + 3) / 4 * 3;
  }

  /// @brief Encodes a byte array to base64.
  /// @param data The data to encode.
  /// @param dataLen The size of the data to encode.
  /// @param output
  /// @param outputLen
  /// @return The amount of bytes written to the output buffer.
  inline std::size_t Encode(const std::uint8_t* data, std::size_t dataLen, char* output, std::size_t outputLen) noexcept {
    std::size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<std::uint8_t*>(output), outputLen, &written, data, dataLen) != 0) {
      return 0;
    }
    return written;
  }

  /// @brief Encodes a byte array to base64.
  /// @param data The data to encode.
  /// @param dataLen The size of the data to encode.
  /// @param output The output string to write to.
  /// @return The amount of bytes written to the output buffer.
  inline std::size_t Encode(const std::uint8_t* data, std::size_t dataLen, std::string& output) noexcept {
    std::size_t outputLen = CalculateEncodedSize(dataLen);
    output.resize(outputLen);
    return Encode(data, dataLen, const_cast<char*>(output.data()), output.size());
  }

  /// @brief Decodes a base64 string.
  /// @param data The data to decode.
  /// @param dataLen The size of the data to decode.
  /// @param output The output buffer to write to.
  /// @param outputLen The size of the output buffer.
  /// @return The amount of bytes written to the output buffer.
  inline std::size_t Decode(const char* data, std::size_t dataLen, std::uint8_t* output, std::size_t outputLen) noexcept {
    std::size_t written = 0;
    if (mbedtls_base64_decode(output, outputLen, &written, reinterpret_cast<const std::uint8_t*>(data), dataLen) != 0) {
      return 0;
    }
    return written;
  }

  /// @brief Decodes a base64 string.
  /// @param data The data to decode.
  /// @param dataLen The size of the data to decode.
  /// @param output The output buffer to write to.
  /// @return The amount of bytes written to the output buffer.
  inline std::size_t Decode(const char* data, std::size_t dataLen, std::vector<std::uint8_t>& output) noexcept {
    std::size_t outputLen = CalculateDecodedSize(dataLen);
    output.resize(outputLen);
    return Decode(data, dataLen, output.data(), outputLen);
  }
}  // namespace OpenShock::Base64Utils
