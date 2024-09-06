#pragma once

#include <cstdint>
#include <string_view>

namespace OpenShock::IntConv {
  bool stoi8(std::string_view str, int8_t& val);
  bool stou8(std::string_view str, uint8_t& val);
  bool stoi16(std::string_view str, int16_t& val);
  bool stou16(std::string_view str, uint16_t& val);
  bool stoi32(std::string_view str, int32_t& val);
  bool stou32(std::string_view str, uint32_t& val);
  bool stoi64(std::string_view str, int64_t& val);
  bool stou64(std::string_view str, uint64_t& val);
}  // namespace OpenShock::IntConv
