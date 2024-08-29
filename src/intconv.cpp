#include "intconv.h"

#include <cstdint>
#include <limits>
#include <type_traits>

template <typename T>
constexpr unsigned int NumDigits() {
    static_assert(std::is_integral<T>::value);
    std::uint64_t num = std::numeric_limits<T>::max();

    
    unsigned int digits = std::is_signed<T>::value ? 2 : 1;
    while (num >= 10) {
        num /= 10;
        digits++;
    }

    return digits;
}

// Base converter
template <typename T>
constexpr bool spanToT(OpenShock::StringView str, T& val) {
  static_assert(std::is_integral<T>::value);
  const T Threshold = std::numeric_limits<T>::max() / 10;

  if (str.Empty()) {
    return false;
  }

  val = 0;
  for (char c : str) {
    if (c < '0' || c > '9') {
        return false;
    }

    if (val > Threshold) {
        return false;
    }

    val *= 10;

    std::uint8_t digit = c - '0';
    if (digit > std::numeric_limits<T>::max() - val) {
        return false;
    }

    val += digit;
  }

  return true;
}

// Unsigned converter
template <typename T>
constexpr bool spanToUT(OpenShock::StringView str, T& val) {
  static_assert(std::is_unsigned<T>::value);

  if (str.length() > NumDigits<T>()) {
    return false;
  }

  return spanToT(str, val);
}

// Signed converter
template <typename T>
constexpr bool spanToST(OpenShock::StringView str, T& val) {
    static_assert(std::is_signed<T>::value);

    if (str.Empty() || str.length() > NumDigits<T>()) {
        return false;
    }

    bool negative = str.front() == '-';
    if (negative) {
        str = str.substr(1);
    }

    typename std::make_unsigned<T>::type i = 0;
    if (!spanToT(str, i)) {
        return false;
    }

    if (i > std::numeric_limits<T>::max()) {
        return false;
    }

    val = negative ? -static_cast<T>(i) : static_cast<T>(i);

    return true;
}

using namespace OpenShock;

// Specific converters
bool IntConv::stoi8(OpenShock::StringView str, std::int8_t& val) {
    return spanToST(str, val);
}
bool IntConv::stou8(OpenShock::StringView str, std::uint8_t& val) {
    return spanToUT(str, val);
}
bool IntConv::stoi16(OpenShock::StringView str, std::int16_t& val) {
    return spanToST(str, val);
}
bool IntConv::stou16(OpenShock::StringView str, std::uint16_t& val) {
    return spanToUT(str, val);
}
bool IntConv::stoi32(OpenShock::StringView str, std::int32_t& val) {
    return spanToST(str, val);
}
bool IntConv::stou32(OpenShock::StringView str, std::uint32_t& val) {
    return spanToUT(str, val);
}
bool IntConv::stoi64(OpenShock::StringView str, std::int64_t& val) {
    return spanToST(str, val);
}
bool IntConv::stou64(OpenShock::StringView str, std::uint64_t& val) {
    return spanToUT(str, val);
}

static_assert(NumDigits<std::uint8_t>() == 3, "NumDigits test for std::uint8_t failed");
static_assert(NumDigits<std::uint16_t>() == 5, "NumDigits test for std::uint16_t failed");
static_assert(NumDigits<std::uint32_t>() == 10, "NumDigits test for std::uint32_t failed");
static_assert(NumDigits<std::uint64_t>() == 20, "NumDigits test for std::uint64_t failed");

static_assert(NumDigits<std::int8_t>() == 4, "NumDigits test for std::int8_t failed");
static_assert(NumDigits<std::int16_t>() == 6, "NumDigits test for std::int16_t failed");
static_assert(NumDigits<std::int32_t>() == 11, "NumDigits test for std::int32_t failed");
static_assert(NumDigits<std::int64_t>() == 20, "NumDigits test for std::int64_t failed");

constexpr bool test_spanToUT8() {
    std::uint8_t u8 = 0;
    return spanToUT("255"_sv, u8) && u8 == 255;
}

static_assert(test_spanToUT8(), "test_spanToUT8 failed");

constexpr bool test_spanToUT16() {
    std::uint16_t u16 = 0;
    return spanToUT("65535"_sv, u16) && u16 == 65535;
}

static_assert(test_spanToUT16(), "test_spanToUT16 failed");

constexpr bool test_spanToUT32() {
    std::uint32_t u32 = 0;
    return spanToUT("4294967295"_sv, u32) && u32 == 4294967295U;
}

static_assert(test_spanToUT32(), "test_spanToUT32 failed");

constexpr bool test_spanToUT64() {
    std::uint64_t u64 = 0;
    return spanToUT("18446744073709551615"_sv, u64) && u64 == 18446744073709551615ULL;
}

static_assert(test_spanToUT64(), "test_spanToUT64 failed");

constexpr bool test_spanToUT8Overflow() {
    std::uint8_t u8 = 0;
    return !spanToUT("256"_sv, u8);  // Overflow
}

static_assert(test_spanToUT8Overflow(), "test_spanToUT8Overflow failed");

constexpr bool test_spanToUT16Overflow() {
    std::uint16_t u16 = 0;
    return !spanToUT("70000"_sv, u16);  // Overflow
}

static_assert(test_spanToUT16Overflow(), "test_spanToUT16Overflow failed");

constexpr bool test_spanToUT32Overflow() {
    std::uint32_t u32 = 0;
    return !spanToUT("4294967296"_sv, u32);  // Overflow
}

static_assert(test_spanToUT32Overflow(), "test_spanToUT32Overflow failed");

constexpr bool test_spanToUT64Overflow() {
    std::uint64_t u64 = 0;
    return !spanToUT("18446744073709551616"_sv, u64);  // Overflow
}

static_assert(test_spanToUT64Overflow(), "test_spanToUT64Overflow failed");

constexpr bool test_spanToST8() {
    std::int8_t i8 = 0;
    return spanToST("-128"_sv, i8) && i8 == -128;
}

static_assert(test_spanToST8(), "test_spanToST8 failed");

constexpr bool test_spanToST16() {
    std::int16_t i16 = 0;
    return spanToST("32767"_sv, i16) && i16 == 32767;
}

static_assert(test_spanToST16(), "test_spanToST16 failed");

constexpr bool test_spanToST32() {
    std::int32_t i32 = 0;
    return spanToST("-2147483648"_sv, i32) && i32 == -2147483648;
}

static_assert(test_spanToST32(), "test_spanToST32 failed");

constexpr bool test_spanToST64() {
    std::int64_t i64 = 0;
    return spanToST("9223372036854775807"_sv, i64) && i64 == 9223372036854775807LL;
}

static_assert(test_spanToST64(), "test_spanToST64 failed");

constexpr bool test_spanToST8Underflow() {
    std::int8_t i8 = 0;
    return !spanToST("-129"_sv, i8);  // Underflow
}

static_assert(test_spanToST8Underflow(), "test_spanToST8Underflow failed");

constexpr bool test_spanToST8Overflow() {
    std::int8_t i8 = 0;
    return !spanToST("128"_sv, i8);  // Overflow
}

static_assert(test_spanToST8Overflow(), "test_spanToST8Overflow failed");

constexpr bool test_spanToST16Underflow() {
    std::int16_t i16 = 0;
    return !spanToST("-32769"_sv, i16);  // Underflow
}

static_assert(test_spanToST16Underflow(), "test_spanToST16Underflow failed");

constexpr bool test_spanToST16Overflow() {
    std::int16_t i16 = 0;
    return !spanToST("32768"_sv, i16);  // Overflow
}

static_assert(test_spanToST16Overflow(), "test_spanToST16Overflow failed");

constexpr bool test_spanToST32Underflow() {
    std::int32_t i32 = 0;
    return !spanToST("-2147483649"_sv, i32);  // Underflow
}

static_assert(test_spanToST32Underflow(), "test_spanToST32Underflow failed");

constexpr bool test_spanToST32Overflow() {
    std::int32_t i32 = 0;
    return !spanToST("2147483648"_sv, i32);  // Overflow
}

static_assert(test_spanToST32Overflow(), "test_spanToST32Overflow failed");

constexpr bool test_spanToST64Underflow() {
    std::int64_t i64 = 0;
    return !spanToST("-9223372036854775809"_sv, i64);  // Underflow
}

static_assert(test_spanToST64Underflow(), "test_spanToST64Underflow failed");

constexpr bool test_spanToST64Overflow() {
    std::int64_t i64 = 0;
    return !spanToST("9223372036854775808"_sv, i64);  // Overflow
}

static_assert(test_spanToST64Overflow(), "test_spanToST64Overflow failed");

constexpr bool test_spanToSTEmptyString() {
    std::int8_t i8 = 0;
    return !spanToST(""_sv, i8);  // Empty string
}

static_assert(test_spanToSTEmptyString(), "test_spanToSTEmptyString failed");

constexpr bool test_spanToSTJustNegativeSign() {
    std::int16_t i16 = 0;
    return !spanToST("-"_sv, i16);  // Just a negative sign
}

static_assert(test_spanToSTJustNegativeSign(), "test_spanToSTJustNegativeSign failed");

constexpr bool test_spanToSTInvalidCharacter() {
    std::int32_t i32 = 0;
    return !spanToST("+123"_sv, i32);  // Invalid character
}

static_assert(test_spanToSTInvalidCharacter(), "test_spanToSTInvalidCharacter failed");

constexpr bool test_spanToSTLeadingSpace() {
    std::int64_t i64 = 0;
    return !spanToST(" 123"_sv, i64);  // Leading space
}

static_assert(test_spanToSTLeadingSpace(), "test_spanToSTLeadingSpace failed");