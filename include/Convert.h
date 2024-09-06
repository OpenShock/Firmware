#pragma once

#include <cstdint>
#include <string_view>

namespace OpenShock::Convert {
  bool FromInt8(std::string_view str, int8_t& val);
  bool FromUint8(std::string_view str, uint8_t& val);
  bool FromInt16(std::string_view str, int16_t& val);
  bool FromUint16(std::string_view str, uint16_t& val);
  bool FromInt32(std::string_view str, int32_t& val);
  bool FromUint32(std::string_view str, uint32_t& val);
  bool FromInt64(std::string_view str, int64_t& val);
  bool FromUint64(std::string_view str, uint64_t& val);
  bool FromBool(std::string_view str, bool& val);
}  // namespace OpenShock::Convert
