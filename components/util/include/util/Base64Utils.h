#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "TinyVec.h"

namespace OpenShock::Base64Utils {
  /// @brief Encodes a byte array to base64.
  /// @param data The data to encode.
  /// @param dataLen The size of the data to encode.
  /// @param output The output string to write to.
  /// @return The amount of bytes written to the output buffer.
  bool Encode(std::span<const uint8_t> data, std::string& output);

  /// @brief Decodes a base64 string.
  /// @param data The data to decode.
  /// @param dataLen The size of the data to decode.
  /// @param output The output buffer to write to.
  /// @return The amount of bytes written to the output buffer.
  bool Decode(std::string_view data, TinyVec<uint8_t>& output) noexcept;
}  // namespace OpenShock::Base64Utils
