#include "Convert.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

using namespace std::string_view_literals;

template<typename T>
constexpr unsigned int NumDigits() {
  static_assert(std::is_integral<T>::value);
  uint64_t num = std::numeric_limits<T>::max();

  unsigned int digits = std::is_signed<T>::value ? 2 : 1;
  while (num >= 10) {
    num /= 10;
    digits++;
  }

  return digits;
}

// Base converter
template<typename T>
constexpr bool spanToT(std::string_view str, T& val) {
  static_assert(std::is_integral<T>::value);
  const T Threshold = std::numeric_limits<T>::max() / 10;

  val = 0;

  // Special case for zero, also handles leading zeros
  if (str.front() == '0') {
    return str.length() == 1;
  }

  for (char c : str) {
    if (c < '0' || c > '9') {
      return false;
    }

    if (val > Threshold) {
      return false;
    }

    val *= 10;

    uint8_t digit = c - '0';
    if (digit > std::numeric_limits<T>::max() - val) {
      return false;
    }

    val += digit;
  }

  return true;
}

// Unsigned converter
template<typename T>
constexpr bool spanToUT(std::string_view str, T& val) {
  static_assert(std::is_unsigned<T>::value);

  if (str.empty() || str.length() > NumDigits<T>()) {
    return false;
  }

  return spanToT(str, val);
}

// Signed converter
template<typename T>
constexpr bool spanToST(std::string_view str, T& val) {
  static_assert(std::is_signed<T>::value);

  if (str.empty() || str.length() > NumDigits<T>()) {
    return false;
  }

  bool negative = str.front() == '-';
  if (negative) {
    str = str.substr(1);
    if (str.empty()) {
      return false;
    }
  }

  typename std::make_unsigned<T>::type i = 0;
  if (!spanToT(str, i)) {
    return false;
  }

  if (i > std::numeric_limits<T>::max()) {
    return false;
  }

  val = negative ? -static_cast<T>(i) : static_cast<T>(i);

  return !(negative && i == 0);  // "-0" is invalid
}

using namespace OpenShock;

// Specific converters
bool Convert::ToInt8(std::string_view str, int8_t& val) {
  return spanToST(str, val);
}
bool Convert::ToUint8(std::string_view str, uint8_t& val) {
  return spanToUT(str, val);
}
bool Convert::ToInt16(std::string_view str, int16_t& val) {
  return spanToST(str, val);
}
bool Convert::ToUint16(std::string_view str, uint16_t& val) {
  return spanToUT(str, val);
}
bool Convert::ToInt32(std::string_view str, int32_t& val) {
  return spanToST(str, val);
}
bool Convert::ToUint32(std::string_view str, uint32_t& val) {
  return spanToUT(str, val);
}
bool Convert::ToInt64(std::string_view str, int64_t& val) {
  return spanToST(str, val);
}
bool Convert::ToUint64(std::string_view str, uint64_t& val) {
  return spanToUT(str, val);
}
bool Convert::ToBool(std::string_view str, bool& val) {
  if (str.length() > 5) {
    return false;
  }

  if (strncasecmp(str.data(), "true", str.length()) == 0) {
    val = true;
    return true;
  }

  if (strncasecmp(str.data(), "false", str.length()) == 0) {
    val = false;
    return true;
  }

  return false;
}

static_assert(NumDigits<uint8_t>() == 3, "NumDigits test for uint8_t failed");
static_assert(NumDigits<uint16_t>() == 5, "NumDigits test for uint16_t failed");
static_assert(NumDigits<uint32_t>() == 10, "NumDigits test for uint32_t failed");
static_assert(NumDigits<uint64_t>() == 20, "NumDigits test for uint64_t failed");

static_assert(NumDigits<int8_t>() == 4, "NumDigits test for int8_t failed");
static_assert(NumDigits<int16_t>() == 6, "NumDigits test for int16_t failed");
static_assert(NumDigits<int32_t>() == 11, "NumDigits test for int32_t failed");
static_assert(NumDigits<int64_t>() == 20, "NumDigits test for int64_t failed");

constexpr bool test_spanToUT8() {
  uint8_t u8 = 0;
  return spanToUT("255"sv, u8) && u8 == 255;
}

static_assert(test_spanToUT8(), "test_spanToUT8 failed");

constexpr bool test_spanToUT16() {
  uint16_t u16 = 0;
  return spanToUT("65535"sv, u16) && u16 == 65'535;
}

static_assert(test_spanToUT16(), "test_spanToUT16 failed");

constexpr bool test_spanToUT32() {
  uint32_t u32 = 0;
  return spanToUT("4294967295"sv, u32) && u32 == 4'294'967'295U;
}

static_assert(test_spanToUT32(), "test_spanToUT32 failed");

constexpr bool test_spanToUT64() {
  uint64_t u64 = 0;
  return spanToUT("18446744073709551615"sv, u64) && u64 == 18'446'744'073'709'551'615ULL;
}

static_assert(test_spanToUT64(), "test_spanToUT64 failed");

constexpr bool test_spanToUT8Overflow() {
  uint8_t u8 = 0;
  return !spanToUT("256"sv, u8);  // Overflow
}

static_assert(test_spanToUT8Overflow(), "test_spanToUT8Overflow failed");

constexpr bool test_spanToUT16Overflow() {
  uint16_t u16 = 0;
  return !spanToUT("70000"sv, u16);  // Overflow
}

static_assert(test_spanToUT16Overflow(), "test_spanToUT16Overflow failed");

constexpr bool test_spanToUT32Overflow() {
  uint32_t u32 = 0;
  return !spanToUT("4294967296"sv, u32);  // Overflow
}

static_assert(test_spanToUT32Overflow(), "test_spanToUT32Overflow failed");

constexpr bool test_spanToUT64Overflow() {
  uint64_t u64 = 0;
  return !spanToUT("18446744073709551616"sv, u64);  // Overflow
}

static_assert(test_spanToUT64Overflow(), "test_spanToUT64Overflow failed");

constexpr bool test_spanToST8() {
  int8_t i8 = 0;
  return spanToST("-127"sv, i8) && i8 == -127;
}

static_assert(test_spanToST8(), "test_spanToST8 failed");

constexpr bool test_spanToST16() {
  int16_t i16 = 0;
  return spanToST("32767"sv, i16) && i16 == 32'767;
}

static_assert(test_spanToST16(), "test_spanToST16 failed");

constexpr bool test_spanToST32() {
  int32_t i32 = 0;
  return spanToST("-2147483647"sv, i32) && i32 == -2'147'483'647;
}

static_assert(test_spanToST32(), "test_spanToST32 failed");

constexpr bool test_spanToST64() {
  int64_t i64 = 0;
  return spanToST("9223372036854775807"sv, i64) && i64 == 9'223'372'036'854'775'807LL;
}

static_assert(test_spanToST64(), "test_spanToST64 failed");

constexpr bool test_spanToST8Underflow() {
  int8_t i8 = 0;
  return !spanToST("-128"sv, i8);  // Underflow
}

static_assert(test_spanToST8Underflow(), "test_spanToST8Underflow failed");

constexpr bool test_spanToST8Overflow() {
  int8_t i8 = 0;
  return !spanToST("128"sv, i8);  // Overflow
}

static_assert(test_spanToST8Overflow(), "test_spanToST8Overflow failed");

constexpr bool test_spanToST16Underflow() {
  int16_t i16 = 0;
  return !spanToST("-32769"sv, i16);  // Underflow
}

static_assert(test_spanToST16Underflow(), "test_spanToST16Underflow failed");

constexpr bool test_spanToST16Overflow() {
  int16_t i16 = 0;
  return !spanToST("32768"sv, i16);  // Overflow
}

static_assert(test_spanToST16Overflow(), "test_spanToST16Overflow failed");

constexpr bool test_spanToST32Underflow() {
  int32_t i32 = 0;
  return !spanToST("-2147483649"sv, i32);  // Underflow
}

static_assert(test_spanToST32Underflow(), "test_spanToST32Underflow failed");

constexpr bool test_spanToST32Overflow() {
  int32_t i32 = 0;
  return !spanToST("2147483648"sv, i32);  // Overflow
}

static_assert(test_spanToST32Overflow(), "test_spanToST32Overflow failed");

constexpr bool test_spanToST64Underflow() {
  int64_t i64 = 0;
  return !spanToST("-9223372036854775809"sv, i64);  // Underflow
}

static_assert(test_spanToST64Underflow(), "test_spanToST64Underflow failed");

constexpr bool test_spanToST64Overflow() {
  int64_t i64 = 0;
  return !spanToST("9223372036854775808"sv, i64);  // Overflow
}

static_assert(test_spanToST64Overflow(), "test_spanToST64Overflow failed");

constexpr bool test_spanToSTEmptyString() {
  int8_t i8 = 0;
  return !spanToST(""sv, i8);  // Empty string
}

static_assert(test_spanToSTEmptyString(), "test_spanToSTEmptyString failed");

constexpr bool test_spanToSTJustNegativeSign() {
  int16_t i16 = 0;
  return !spanToST("-"sv, i16);  // Just a negative sign
}

static_assert(test_spanToSTJustNegativeSign(), "test_spanToSTJustNegativeSign failed");

constexpr bool test_spanToSTNegativeZero() {
  int32_t i32 = 0;
  return !spanToST("-0"sv, i32);  // Negative zero
}

static_assert(test_spanToSTNegativeZero(), "test_spanToSTNegativeZero failed");

constexpr bool test_spanToSTInvalidCharacter() {
  int32_t i32 = 0;
  return !spanToST("+123"sv, i32);  // Invalid character
}

static_assert(test_spanToSTInvalidCharacter(), "test_spanToSTInvalidCharacter failed");

constexpr bool test_spanToSTLeadingSpace() {
  int64_t i64 = 0;
  return !spanToST(" 123"sv, i64);  // Leading space
}

static_assert(test_spanToSTLeadingSpace(), "test_spanToSTLeadingSpace failed");

constexpr bool test_spanToSTTrailingSpace() {
  int64_t i64 = 0;
  return !spanToST("123 "sv, i64);  // Trailing space
}

static_assert(test_spanToSTTrailingSpace(), "test_spanToSTTrailingSpace failed");

constexpr bool test_spanToSTLeadingZero() {
  int64_t i64 = 0;
  return !spanToST("0123"sv, i64);  // Leading zero
}

static_assert(test_spanToSTLeadingZero(), "test_spanToSTLeadingZero failed");
