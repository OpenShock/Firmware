#include "Convert.h"

#include "StringHelpers.h"

#include <cstdint>
#include <limits>
#include <string_view>

using namespace std::string_view_literals;

using namespace OpenShock;

std::optional<bool> Convert::ToBool(std::string_view str)
{
  if (str.length() > 5) {
    return std::nullopt;
  }

  if (OpenShock::StringIEquals(str, "true"sv)) {
    return true;
  }

  if (OpenShock::StringIEquals(str, "false"sv)) {
    return false;
  }

  return std::nullopt;
}

std::optional<gpio_num_t> Convert::ToGpioNum(std::string_view str)
{
  auto i8 = ToInt8(str);
  if (!i8) {
    return std::nullopt;
  }

  if (*i8 < GPIO_NUM_NC || *i8 >= GPIO_NUM_MAX) {
    return std::nullopt;
  }

  return static_cast<gpio_num_t>(*i8);
}

// Compile-time self-tests for Convert::ToInteger (constexpr std::from_chars).

// Zero and per-width limits
static_assert(Convert::ToInteger<int>("0"sv) == 0, "ToInteger zero failed");

static_assert(Convert::ToUint8("255"sv) == std::numeric_limits<uint8_t>::max(), "ToUint8 max failed");
static_assert(Convert::ToUint16("65535"sv) == std::numeric_limits<uint16_t>::max(), "ToUint16 max failed");
static_assert(Convert::ToUint32("4294967295"sv) == std::numeric_limits<uint32_t>::max(), "ToUint32 max failed");
static_assert(Convert::ToUint64("18446744073709551615"sv) == std::numeric_limits<uint64_t>::max(), "ToUint64 max failed");

static_assert(!Convert::ToUint8("256"sv).has_value(), "ToUint8 overflow failed");
static_assert(!Convert::ToUint16("65536"sv).has_value(), "ToUint16 overflow failed");
static_assert(!Convert::ToUint32("4294967296"sv).has_value(), "ToUint32 overflow failed");
static_assert(!Convert::ToUint64("18446744073709551616"sv).has_value(), "ToUint64 overflow failed");

static_assert(Convert::ToInt8("-128"sv) == std::numeric_limits<int8_t>::min(), "ToInt8 min failed");
static_assert(Convert::ToInt8("127"sv) == std::numeric_limits<int8_t>::max(), "ToInt8 max failed");
static_assert(Convert::ToInt16("-32768"sv) == std::numeric_limits<int16_t>::min(), "ToInt16 min failed");
static_assert(Convert::ToInt16("32767"sv) == std::numeric_limits<int16_t>::max(), "ToInt16 max failed");
static_assert(Convert::ToInt32("-2147483648"sv) == std::numeric_limits<int32_t>::min(), "ToInt32 min failed");
static_assert(Convert::ToInt32("2147483647"sv) == std::numeric_limits<int32_t>::max(), "ToInt32 max failed");
static_assert(Convert::ToInt64("-9223372036854775808"sv) == std::numeric_limits<int64_t>::min(), "ToInt64 min failed");
static_assert(Convert::ToInt64("9223372036854775807"sv) == std::numeric_limits<int64_t>::max(), "ToInt64 max failed");

static_assert(!Convert::ToInt8("-129"sv).has_value(), "ToInt8 underflow failed");
static_assert(!Convert::ToInt8("128"sv).has_value(), "ToInt8 overflow failed");
static_assert(!Convert::ToInt16("-32769"sv).has_value(), "ToInt16 underflow failed");
static_assert(!Convert::ToInt16("32768"sv).has_value(), "ToInt16 overflow failed");
static_assert(!Convert::ToInt32("-2147483649"sv).has_value(), "ToInt32 underflow failed");
static_assert(!Convert::ToInt32("2147483648"sv).has_value(), "ToInt32 overflow failed");
static_assert(!Convert::ToInt64("-9223372036854775809"sv).has_value(), "ToInt64 underflow failed");
static_assert(!Convert::ToInt64("9223372036854775808"sv).has_value(), "ToInt64 overflow failed");

// Malformed input
static_assert(!Convert::ToInteger<int>(""sv).has_value(), "ToInteger empty string failed");
static_assert(!Convert::ToInteger<int>("-"sv).has_value(), "ToInteger lone negative sign failed");
static_assert(!Convert::ToInteger<int>("-0"sv).has_value(), "ToInteger negative zero failed");
static_assert(!Convert::ToInteger<int>("+123"sv).has_value(), "ToInteger plus sign failed");
static_assert(!Convert::ToInteger<int>(" 123"sv).has_value(), "ToInteger leading space failed");
static_assert(!Convert::ToInteger<int>("123 "sv).has_value(), "ToInteger trailing space failed");
static_assert(!Convert::ToInteger<int>("0123"sv).has_value(), "ToInteger leading zero failed");
static_assert(!Convert::ToInteger<int>("-0123"sv).has_value(), "ToInteger negative leading zero failed");
static_assert(!Convert::ToInteger<int>("12x"sv).has_value(), "ToInteger trailing junk failed");
static_assert(!Convert::ToInteger<unsigned>("-1"sv).has_value(), "ToInteger negative unsigned failed");
static_assert(!Convert::ToInteger<unsigned>("-0"sv).has_value(), "ToInteger negative zero unsigned failed");
