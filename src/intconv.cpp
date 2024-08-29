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

// spanToUT Tests
constexpr bool test_spanToUT() {
    std::uint8_t u8 = 0;
    if (!spanToUT("255"_sv, u8) || u8 != 255) return false;

    std::uint16_t u16 = 0;
    if (!spanToUT("65535"_sv, u16) || u16 != 65535) return false;

    std::uint32_t u32 = 0;
    if (!spanToUT("4294967295"_sv, u32) || u32 != 4294967295U) return false;

    std::uint64_t u64 = 0;
    if (!spanToUT("18446744073709551615"_sv, u64) || u64 != 18446744073709551615ULL) return false;

    return true;
}

static_assert(test_spanToUT(), "spanToUT tests failed");

// spanToUT Overflow Tests
constexpr bool test_spanToUTOverflow() {
    std::uint8_t u8 = 0;
    if (spanToUT("256"_sv, u8)) return false;  // Overflow

    std::uint16_t u16 = 0;
    if (spanToUT("70000"_sv, u16)) return false;  // Overflow

    std::uint32_t u32 = 0;
    if (spanToUT("4294967296"_sv, u32)) return false;  // Overflow

    std::uint64_t u64 = 0;
    if (spanToUT("18446744073709551616"_sv, u64)) return false;  // Overflow

    return true;
}

static_assert(test_spanToUTOverflow(), "spanToUT overflow tests failed");

// spanToST Tests
constexpr bool test_spanToST() {
    std::int8_t i8 = 0;
    if (!spanToST("-128"_sv, i8) || i8 != -128) return false;

    std::int16_t i16 = 0;
    if (!spanToST("32767"_sv, i16) || i16 != 32767) return false;

    std::int32_t i32 = 0;
    if (!spanToST("-2147483648"_sv, i32) || i32 != -2147483648) return false;

    std::int64_t i64 = 0;
    if (!spanToST("9223372036854775807"_sv, i64) || i64 != 9223372036854775807LL) return false;

    return true;
}

static_assert(test_spanToST(), "spanToST tests failed");

// spanToST Overflow Tests
constexpr bool test_spanToSTOverflow() {
    std::int8_t i8 = 0;
    if (spanToST("-129"_sv, i8)) return false;  // Underflow
    if (spanToST("128"_sv, i8)) return false;  // Overflow

    std::int16_t i16 = 0;
    if (spanToST("-32769"_sv, i16)) return false;  // Underflow
    if (spanToST("32768"_sv, i16)) return false;  // Overflow

    std::int32_t i32 = 0;
    if (spanToST("-2147483649"_sv, i32)) return false;  // Underflow
    if (spanToST("2147483648"_sv, i32)) return false;  // Overflow

    std::int64_t i64 = 0;
    if (spanToST("-9223372036854775809"_sv, i64)) return false;  // Underflow
    if (spanToST("9223372036854775808"_sv, i64)) return false;  // Overflow

    return true;
}

static_assert(test_spanToSTOverflow(), "spanToST overflow tests failed");

// Edge Cases Tests
constexpr bool test_edgeCases() {
    std::int8_t i8 = 0;
    if (spanToST(""_sv, i8)) return false;  // Empty string

    std::int16_t i16 = 0;
    if (spanToST("-"_sv, i16)) return false;  // Just a negative sign

    std::int32_t i32 = 0;
    if (spanToST("+123"_sv, i32)) return false;  // Invalid character

    std::int64_t i64 = 0;
    if (spanToST(" 123"_sv, i64)) return false;  // Leading space

    return true;
}

static_assert(test_edgeCases(), "Edge cases tests failed");