#include "Convert.h"

#include "util/DigitCounter.h"
#include "util/StringUtils.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

using namespace std::string_view_literals;

// Base converter
template<typename T>
void fromNonZeroT(T val, std::string& out)
{
  // Ensure the template type T is an integral type
  static_assert(std::is_integral_v<T>, "T must be an integral type");
  using U = std::make_unsigned_t<T>;

  constexpr std::size_t MaxDigits = OpenShock::Util::Digits10CountMax<T>;

  char buf[MaxDigits];

  // Start from the end of the buffer to construct the number in reverse (from least to most significant digit)
  char* const end = buf + MaxDigits;
  char* ptr       = end;

  U u;
  bool negative = false;

  if constexpr (std::is_signed_v<T>) {
    if (val < 0) {
      negative = true;

      // Convert to unsigned, then take the modular negation.
      // This is well-defined and yields the magnitude of val.
      u = U(0) - U(val);
    } else {
      u = U(val);
    }
  } else {
    // Unsigned types: just use the value as-is.
    u = U(val);
  }

  // Extract digits and store them in reverse order in the buffer
  while (u > 0) {
    *--ptr = char('0' + (u % 10));
    u /= 10;
  }

  // If the number was negative, add the negative sign
  if (negative) {
    *--ptr = '-';
  }

  // Append the resulting string to the output
  out.append(ptr, end - ptr);
}

// Base converter
template<typename T>
void fromT(T val, std::string& str)
{
  static_assert(std::is_integral_v<T>);

  if (val == 0) {
    str.push_back('0');
    return;
  }

  fromNonZeroT(val, str);
}

/**
 * @brief Converts a string view to an integral value of type T.
 *
 * This function attempts to parse a string representation of a number into an integer of the specified integral type T.
 * It performs validation to ensure the string is a valid representation of a number within the range of the target type and checks for overflow or invalid input.
 *
 * @tparam T The integral type to which the string should be converted.
 * @param[in] str The input string view representing the number.
 * @param[out] out The reference where the converted value will be stored, if successful.
 * @return true if the conversion was successful and `out` contains the parsed value.
 * @return false if the conversion failed due to invalid input, overflow, or out-of-range value.
 *
 * @note The function handles both signed and unsigned types, for signed types, it processes negative numbers correctly.
 * @note It also validates the input for edge cases like empty strings, leading zeros, or excessive length.
 *
 * @warning Ensure the input string is sanitized if sourced from untrusted input, as this function does not strip whitespace or handle non-numeric characters beyond validation.
 */
template<typename T>
constexpr bool spanToT(std::string_view str, T& out)
{
  static_assert(std::is_integral_v<T>, "Template type T must be an integral type.");

  // Define the unsigned type equivalent for T.
  using UT = typename std::make_unsigned<T>::type;

  constexpr bool IsSigned = std::is_signed_v<T>;

  // Define an overflow threshold for type T.
  // If an integer of type T exceeds this value, multiplying it by 10 will cause an overflow.
  constexpr T Threshold = std::numeric_limits<T>::max() / 10;

  // Fail on empty input strings.
  if (str.empty()) {
    return false;
  }

  // Handle negative numbers for signed types.
  bool isNegative = false;
  if constexpr (IsSigned) {
    if (str.front() == '-') {
      str.remove_prefix(1);  // Remove the negative sign.
      isNegative = true;

      // Fail if the string only contained a negative sign with no numeric characters
      if (str.empty()) {
        return false;
      }
    }
  }

  // Fail on strings that are too long to be valid integers.
  if (str.length() > OpenShock::Util::Digits10CountMax<T>) {
    return false;
  }

  // Special case for zero, also handles leading zeros and negative zero
  if (str.front() == '0') {
    // Fail if string has leading or negative zero's
    if (str.length() > 1 || isNegative) return false;

    // Simple "0" string
    out = 0;
    return true;
  }

  // Value accumulator.
  UT val = 0;

  for (char c : str) {
    // Return false if the character is not a digit.
    if (c < '0' || c > '9') {
      return false;
    }

    // Check for multiplication overflow before multiplying by 10.
    if (val > Threshold) {
      return false;
    }

    val *= 10;

    // Convert the character to a digit and check for addition overflow.
    uint8_t digit = c - '0';
    if (digit > std::numeric_limits<UT>::max() - val) {
      return false;
    }

    val += digit;
  }

  // Special case handling for signed integers.
  if constexpr (IsSigned) {
    if (isNegative) {
      // Signed cast undeflow check, checks against the inverse lower limit for the signed type (e.g., 128 for int8_t).
      if (val > static_cast<UT>(std::numeric_limits<T>::max()) + 1) {
        return false;
      }

      // Assign negative value bits
      val = ~val + 1;
    } else if (val > std::numeric_limits<T>::max()) {
      // Fail on signed cast overflow
      return false;
    }
  }

  // Cast result to output and return
  out = static_cast<T>(val);
  return true;
}

using namespace OpenShock;

// Specific converters
void Convert::FromInt8(int8_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromUint8(uint8_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromInt16(int16_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromUint16(uint16_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromInt32(int32_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromUint32(uint32_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromInt64(int64_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromUint64(uint64_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromSizeT(size_t val, std::string& str)
{
  fromT(val, str);
}

void Convert::FromBool(bool val, std::string& str)
{
  if (val) {
    str += "true";
  } else {
    str += "false";
  }
}

void Convert::FromGpioNum(gpio_num_t val, std::string& str)
{
  fromT(static_cast<int8_t>(val), str);
}

bool Convert::ToInt8(std::string_view str, int8_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToUint8(std::string_view str, uint8_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToInt16(std::string_view str, int16_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToUint16(std::string_view str, uint16_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToInt32(std::string_view str, int32_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToUint32(std::string_view str, uint32_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToInt64(std::string_view str, int64_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToUint64(std::string_view str, uint64_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToSizeT(std::string_view str, size_t& val)
{
  return spanToT(str, val);
}
bool Convert::ToBool(std::string_view str, bool& val)
{
  if (str.length() > 5) {
    return false;
  }

  if (OpenShock::StringIEquals(str, "true"sv)) {
    val = true;
    return true;
  }

  if (OpenShock::StringIEquals(str, "false"sv)) {
    val = false;
    return true;
  }

  return false;
}

bool Convert::ToGpioNum(std::string_view str, gpio_num_t& val)
{
  int8_t i8 = 0;
  if (!spanToT(str, i8)) {
    return false;
  }

  if (i8 < GPIO_NUM_NC || i8 >= GPIO_NUM_MAX) {
    return false;
  }

  val = static_cast<gpio_num_t>(i8);

  return true;
}

constexpr bool test_spanToZero()
{
  int i = 21;
  return spanToT("0"sv, i) && i == 0;
}
static_assert(test_spanToZero(), "test_spanToZero failed");

constexpr bool test_spanToUT8()
{
  constexpr uint8_t U8MAX = std::numeric_limits<uint8_t>::max();

  uint8_t u8 = 0;
  return spanToT("255"sv, u8) && u8 == U8MAX;
}
static_assert(test_spanToUT8(), "test_spanToUT8 failed");

constexpr bool test_spanToUT16()
{
  constexpr uint16_t U16MAX = std::numeric_limits<uint16_t>::max();

  uint16_t u16 = 0;
  return spanToT("65535"sv, u16) && u16 == U16MAX;
}
static_assert(test_spanToUT16(), "test_spanToUT16 failed");

constexpr bool test_spanToUT32()
{
  constexpr uint32_t U32MAX = std::numeric_limits<uint32_t>::max();

  uint32_t u32 = 0;
  return spanToT("4294967295"sv, u32) && u32 == U32MAX;
}
static_assert(test_spanToUT32(), "test_spanToUT32 failed");

constexpr bool test_spanToUT64()
{
  constexpr uint64_t U64MAX = std::numeric_limits<uint64_t>::max();

  uint64_t u64 = 0;
  return spanToT("18446744073709551615"sv, u64) && u64 == U64MAX;
}
static_assert(test_spanToUT64(), "test_spanToUT64 failed");

constexpr bool test_spanToUT8Overflow()
{
  uint8_t u8 = 0;
  return !spanToT("256"sv, u8);  // Overflow
}
static_assert(test_spanToUT8Overflow(), "test_spanToUT8Overflow failed");

constexpr bool test_spanToUT16Overflow()
{
  uint16_t u16 = 0;
  return !spanToT("65536"sv, u16);  // Overflow
}
static_assert(test_spanToUT16Overflow(), "test_spanToUT16Overflow failed");

constexpr bool test_spanToUT32Overflow()
{
  uint32_t u32 = 0;
  return !spanToT("4294967296"sv, u32);  // Overflow
}
static_assert(test_spanToUT32Overflow(), "test_spanToUT32Overflow failed");

constexpr bool test_spanToUT64Overflow()
{
  uint64_t u64 = 0;
  return !spanToT("18446744073709551616"sv, u64);  // Overflow
}
static_assert(test_spanToUT64Overflow(), "test_spanToUT64Overflow failed");

constexpr bool test_spanToT8()
{
  constexpr int8_t I8MIN = std::numeric_limits<int8_t>::min();
  constexpr int8_t I8MAX = std::numeric_limits<int8_t>::max();

  int8_t i8 = 0;
  if (!spanToT("-128"sv, i8) || i8 != I8MIN) return false;
  return spanToT("127"sv, i8) && i8 == I8MAX;
}
static_assert(test_spanToT8(), "test_spanToT8 failed");

constexpr bool test_spanToT16()
{
  constexpr int16_t I16MIN = std::numeric_limits<int16_t>::min();
  constexpr int16_t I16MAX = std::numeric_limits<int16_t>::max();

  int16_t i16 = 0;
  if (!spanToT("-32768"sv, i16) || i16 != I16MIN) return false;
  return spanToT("32767"sv, i16) && i16 == I16MAX;
}
static_assert(test_spanToT16(), "test_spanToT16 failed");

constexpr bool test_spanToT32()
{
  constexpr int32_t I32MIN = std::numeric_limits<int32_t>::min();
  constexpr int32_t I32MAX = std::numeric_limits<int32_t>::max();

  int32_t i32 = 0;
  if (!spanToT("-2147483648"sv, i32) || i32 != I32MIN) return false;
  return spanToT("2147483647"sv, i32) && i32 == I32MAX;
}
static_assert(test_spanToT32(), "test_spanToT32 failed");

constexpr bool test_spanToT64()
{
  constexpr int64_t I64MIN = std::numeric_limits<int64_t>::min();
  constexpr int64_t I64MAX = std::numeric_limits<int64_t>::max();

  int64_t i64 = 0;
  if (!spanToT("-9223372036854775808"sv, i64) || i64 != I64MIN) return false;
  return spanToT("9223372036854775807"sv, i64) && i64 == I64MAX;
}
static_assert(test_spanToT64(), "test_spanToT64 failed");

constexpr bool test_spanToT8Underflow()
{
  int8_t i8 = 0;
  return !spanToT("-129"sv, i8);  // Underflow
}
static_assert(test_spanToT8Underflow(), "test_spanToT8Underflow failed");

constexpr bool test_spanToT8Overflow()
{
  int8_t i8 = 0;
  return !spanToT("128"sv, i8);  // Overflow
}
static_assert(test_spanToT8Overflow(), "test_spanToT8Overflow failed");

constexpr bool test_spanToT16Underflow()
{
  int16_t i16 = 0;
  return !spanToT("-32769"sv, i16);  // Underflow
}
static_assert(test_spanToT16Underflow(), "test_spanToT16Underflow failed");

constexpr bool test_spanToT16Overflow()
{
  int16_t i16 = 0;
  return !spanToT("32768"sv, i16);  // Overflow
}
static_assert(test_spanToT16Overflow(), "test_spanToT16Overflow failed");

constexpr bool test_spanToT32Underflow()
{
  int32_t i32 = 0;
  return !spanToT("-2147483649"sv, i32);  // Underflow
}
static_assert(test_spanToT32Underflow(), "test_spanToT32Underflow failed");

constexpr bool test_spanToT32Overflow()
{
  int32_t i32 = 0;
  return !spanToT("2147483648"sv, i32);  // Overflow
}
static_assert(test_spanToT32Overflow(), "test_spanToT32Overflow failed");

constexpr bool test_spanToT64Underflow()
{
  int64_t i64 = 0;
  return !spanToT("-9223372036854775809"sv, i64);  // Underflow
}
static_assert(test_spanToT64Underflow(), "test_spanToT64Underflow failed");

constexpr bool test_spanToT64Overflow()
{
  int64_t i64 = 0;
  return !spanToT("9223372036854775808"sv, i64);  // Overflow
}
static_assert(test_spanToT64Overflow(), "test_spanToT64Overflow failed");

constexpr bool test_spanToTEmptyString()
{
  int i = 0;
  return !spanToT(""sv, i);  // Empty string
}
static_assert(test_spanToTEmptyString(), "test_spanToTEmptyString failed");

constexpr bool test_spanToTJustNegativeSign()
{
  int i = 0;
  return !spanToT("-"sv, i);  // Just a negative sign
}
static_assert(test_spanToTJustNegativeSign(), "test_spanToTJustNegativeSign failed");

constexpr bool test_spanToTNegativeZero()
{
  int i = 0;
  return !spanToT("-0"sv, i);  // Negative zero
}
static_assert(test_spanToTNegativeZero(), "test_spanToTNegativeZero failed");

constexpr bool test_spanToTInvalidCharacter()
{
  int i = 0;
  return !spanToT("+123"sv, i);  // Invalid character
}
static_assert(test_spanToTInvalidCharacter(), "test_spanToTInvalidCharacter failed");

constexpr bool test_spanToTLeadingSpace()
{
  int i = 0;
  return !spanToT(" 123"sv, i);  // Leading space
}
static_assert(test_spanToTLeadingSpace(), "test_spanToTLeadingSpace failed");

constexpr bool test_spanToTTrailingSpace()
{
  int i = 0;
  return !spanToT("123 "sv, i);  // Trailing space
}
static_assert(test_spanToTTrailingSpace(), "test_spanToTTrailingSpace failed");

constexpr bool test_spanToTLeadingZero()
{
  int i = 0;
  return !spanToT("0123"sv, i);  // Leading zero
}
static_assert(test_spanToTLeadingZero(), "test_spanToTLeadingZero failed");
