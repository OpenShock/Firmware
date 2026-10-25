// Convert::To* string -> integer/bool parsers (used for serial args, config,
// backend responses). Range enforcement matters: an out-of-range shocker id or
// intensity must be rejected, not truncated.
#include "unity.h"

#include "Convert.h"

#include <cstdint>
#include <string>
#include <string_view>

using namespace OpenShock;

TEST_CASE("Convert::ToUint8 range", "[util][convert]")
{
  auto v = Convert::ToUint8("0");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_UINT8(0, *v);
  v = Convert::ToUint8("255");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_UINT8(255, *v);
  v = Convert::ToUint8("42");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_UINT8(42, *v);

  TEST_ASSERT_FALSE(Convert::ToUint8("256").has_value());  // overflow
  TEST_ASSERT_FALSE(Convert::ToUint8("-1").has_value());   // negative
  TEST_ASSERT_FALSE(Convert::ToUint8("").has_value());     // empty
  TEST_ASSERT_FALSE(Convert::ToUint8("abc").has_value());  // not a number
  TEST_ASSERT_FALSE(Convert::ToUint8("12x").has_value());  // trailing junk
}

TEST_CASE("Convert::ToInt8 signed range", "[util][convert]")
{
  auto v = Convert::ToInt8("-128");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT8(-128, *v);
  v = Convert::ToInt8("127");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT8(127, *v);

  TEST_ASSERT_FALSE(Convert::ToInt8("128").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt8("-129").has_value());
}

TEST_CASE("Convert::ToUint16 / ToInt32 boundaries", "[util][convert]")
{
  auto u16 = Convert::ToUint16("65535");
  TEST_ASSERT_TRUE(u16.has_value());
  TEST_ASSERT_EQUAL_UINT16(65535, *u16);
  TEST_ASSERT_FALSE(Convert::ToUint16("65536").has_value());

  auto i32 = Convert::ToInt32("-2147483648");
  TEST_ASSERT_TRUE(i32.has_value());
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, *i32);
  i32 = Convert::ToInt32("2147483647");
  TEST_ASSERT_TRUE(i32.has_value());
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, *i32);
  TEST_ASSERT_FALSE(Convert::ToInt32("2147483648").has_value());
}

TEST_CASE("Convert::ToUint64 large values", "[util][convert]")
{
  auto v = Convert::ToUint64("18446744073709551615");  // UINT64_MAX
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, *v);
  TEST_ASSERT_FALSE(Convert::ToUint64("18446744073709551616").has_value());  // overflow
}

TEST_CASE("Convert: min/max per width, and one past them", "[util][convert]")
{
  TEST_ASSERT_TRUE(Convert::ToInt16("-32768") == INT16_MIN);
  TEST_ASSERT_TRUE(Convert::ToInt16("32767") == INT16_MAX);
  TEST_ASSERT_FALSE(Convert::ToInt16("-32769").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt16("32768").has_value());

  TEST_ASSERT_TRUE(Convert::ToUint32("4294967295") == UINT32_MAX);
  TEST_ASSERT_FALSE(Convert::ToUint32("4294967296").has_value());

  TEST_ASSERT_TRUE(Convert::ToInt64("-9223372036854775808") == INT64_MIN);
  TEST_ASSERT_TRUE(Convert::ToInt64("9223372036854775807") == INT64_MAX);
  TEST_ASSERT_FALSE(Convert::ToInt64("-9223372036854775809").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt64("9223372036854775808").has_value());

  // Far too many digits is an overflow too, not a truncation
  TEST_ASSERT_FALSE(Convert::ToUint8("100000000000000000000000000000").has_value());

  TEST_ASSERT_TRUE(Convert::ToSizeT("0") == static_cast<std::size_t>(0));
  TEST_ASSERT_TRUE(Convert::ToSizeT("4294967295") == static_cast<std::size_t>(4294967295u));
}

TEST_CASE("Convert: strict number syntax (from_chars alone is laxer)", "[util][convert]")
{
  // Leading zeros: std::from_chars would accept these
  TEST_ASSERT_FALSE(Convert::ToUint8("007").has_value());
  TEST_ASSERT_FALSE(Convert::ToUint8("00").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("-007").has_value());

  // Negative zero, on signed and unsigned types
  TEST_ASSERT_FALSE(Convert::ToInt32("-0").has_value());
  TEST_ASSERT_FALSE(Convert::ToUint32("-0").has_value());

  // Explicit plus sign, a lone sign
  TEST_ASSERT_FALSE(Convert::ToInt32("+1").has_value());
  TEST_ASSERT_FALSE(Convert::ToUint32("+1").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("-").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("+").has_value());

  // Whitespace anywhere
  TEST_ASSERT_FALSE(Convert::ToInt32(" 1").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("1 ").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("\t1").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("1\n").has_value());

  // Trailing junk / other bases
  TEST_ASSERT_FALSE(Convert::ToInt32("1x").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("1.0").has_value());
  TEST_ASSERT_FALSE(Convert::ToInt32("0x10").has_value());

  // A plain zero is still fine, and a non-NUL-terminated view only parses its own characters
  TEST_ASSERT_TRUE(Convert::ToInt32("0") == 0);
  TEST_ASSERT_TRUE(Convert::ToInt32(std::string_view("123456", 3)) == 123);
}

TEST_CASE("Convert::ToBool", "[util][convert]")
{
  auto b = Convert::ToBool("true");
  TEST_ASSERT_TRUE(b.has_value());
  TEST_ASSERT_TRUE(*b);
  b = Convert::ToBool("false");
  TEST_ASSERT_TRUE(b.has_value());
  TEST_ASSERT_FALSE(*b);
  b = Convert::ToBool("TRUE");  // case-insensitive
  TEST_ASSERT_TRUE(b.has_value());
  TEST_ASSERT_TRUE(*b);

  TEST_ASSERT_FALSE(Convert::ToBool("yes").has_value());
  TEST_ASSERT_FALSE(Convert::ToBool("1").has_value());
  TEST_ASSERT_FALSE(Convert::ToBool("").has_value());
}

TEST_CASE("Convert::ToGpioNum", "[util][convert]")
{
  TEST_ASSERT_TRUE(Convert::ToGpioNum("-1") == GPIO_NUM_NC);
  TEST_ASSERT_TRUE(Convert::ToGpioNum("0") == GPIO_NUM_0);  // 0 is a valid pin, not a failure

  // The upper bound is the target's GPIO_NUM_MAX (exclusive); ToGpioNum parses through int8_t
  constexpr int Max = static_cast<int>(GPIO_NUM_MAX);
  if constexpr (Max <= 128) {
    TEST_ASSERT_TRUE(Convert::ToGpioNum(std::to_string(Max - 1)) == static_cast<gpio_num_t>(Max - 1));
    TEST_ASSERT_FALSE(Convert::ToGpioNum(std::to_string(Max)).has_value());
  }

  TEST_ASSERT_FALSE(Convert::ToGpioNum("-2").has_value());
  TEST_ASSERT_FALSE(Convert::ToGpioNum("").has_value());
  TEST_ASSERT_FALSE(Convert::ToGpioNum("04").has_value());
}
