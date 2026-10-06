#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "TinyVec.h"

namespace OpenShock::Base64 {
  /// @brief Encodes a byte array to base64.
  /// @param data The data to encode.
  /// @param output Receives the encoded text; cleared on failure.
  /// @return True on success.
  bool Encode(std::span<const uint8_t> data, std::string& output);

  /// @brief Decodes a base64 string.
  /// @param data The base64 text to decode.
  /// @param output Receives the decoded bytes (sized to exactly what was decoded); cleared on failure.
  /// @return True on success, false on invalid input.
  bool Decode(std::string_view data, TinyVec<uint8_t>& output) noexcept;
}  // namespace OpenShock::Base64
