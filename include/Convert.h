#pragma once

#include <hal/gpio_types.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace OpenShock::Convert {  // TODO: C++23 make this use std::from_chars instead
  void FromInt8(int8_t val, std::string& str);
  void FromUint8(uint8_t val, std::string& str);
  void FromInt16(int16_t val, std::string& str);
  void FromUint16(uint16_t val, std::string& str);
  void FromInt32(int32_t val, std::string& str);
  void FromUint32(uint32_t val, std::string& str);
  void FromInt64(int64_t val, std::string& str);
  void FromUint64(uint64_t val, std::string& str);
  void FromBool(bool val, std::string& str);
  void FromGpioNum(gpio_num_t val, std::string& str);

  bool ToInt8(std::string_view str, int8_t& val);
  bool ToUint8(std::string_view str, uint8_t& val);
  bool ToInt16(std::string_view str, int16_t& val);
  bool ToUint16(std::string_view str, uint16_t& val);
  bool ToInt32(std::string_view str, int32_t& val);
  bool ToUint32(std::string_view str, uint32_t& val);
  bool ToInt64(std::string_view str, int64_t& val);
  bool ToUint64(std::string_view str, uint64_t& val);
  bool ToSizeT(std::string_view str, size_t& val);
  bool ToBool(std::string_view str, bool& val);
  bool ToGpioNum(std::string_view str, gpio_num_t& val);
}  // namespace OpenShock::Convert
