// Convert::To* string -> integer/bool parsers (used for serial args, config,
// backend responses). Range enforcement matters: an out-of-range shocker id or
// intensity must be rejected, not truncated.
#include "unity.h"

#include "Convert.h"

#include <cstdint>

using namespace OpenShock;

TEST_CASE("Convert::ToUint8 range", "[util][convert]")
{
  uint8_t v = 0;
  TEST_ASSERT_TRUE(Convert::ToUint8("0", v));
  TEST_ASSERT_EQUAL_UINT8(0, v);
  TEST_ASSERT_TRUE(Convert::ToUint8("255", v));
  TEST_ASSERT_EQUAL_UINT8(255, v);
  TEST_ASSERT_TRUE(Convert::ToUint8("42", v));
  TEST_ASSERT_EQUAL_UINT8(42, v);

  TEST_ASSERT_FALSE(Convert::ToUint8("256", v));  // overflow
  TEST_ASSERT_FALSE(Convert::ToUint8("-1", v));   // negative
  TEST_ASSERT_FALSE(Convert::ToUint8("", v));     // empty
  TEST_ASSERT_FALSE(Convert::ToUint8("abc", v));  // not a number
  TEST_ASSERT_FALSE(Convert::ToUint8("12x", v));  // trailing junk
}

TEST_CASE("Convert::ToInt8 signed range", "[util][convert]")
{
  int8_t v = 0;
  TEST_ASSERT_TRUE(Convert::ToInt8("-128", v));
  TEST_ASSERT_EQUAL_INT8(-128, v);
  TEST_ASSERT_TRUE(Convert::ToInt8("127", v));
  TEST_ASSERT_EQUAL_INT8(127, v);

  TEST_ASSERT_FALSE(Convert::ToInt8("128", v));
  TEST_ASSERT_FALSE(Convert::ToInt8("-129", v));
}

TEST_CASE("Convert::ToUint16 / ToInt32 boundaries", "[util][convert]")
{
  uint16_t u16 = 0;
  TEST_ASSERT_TRUE(Convert::ToUint16("65535", u16));
  TEST_ASSERT_EQUAL_UINT16(65535, u16);
  TEST_ASSERT_FALSE(Convert::ToUint16("65536", u16));

  int32_t i32 = 0;
  TEST_ASSERT_TRUE(Convert::ToInt32("-2147483648", i32));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, i32);
  TEST_ASSERT_TRUE(Convert::ToInt32("2147483647", i32));
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, i32);
  TEST_ASSERT_FALSE(Convert::ToInt32("2147483648", i32));
}

TEST_CASE("Convert::ToUint64 large values", "[util][convert]")
{
  uint64_t v = 0;
  TEST_ASSERT_TRUE(Convert::ToUint64("18446744073709551615", v));  // UINT64_MAX
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, v);
  TEST_ASSERT_FALSE(Convert::ToUint64("18446744073709551616", v));  // overflow
}

TEST_CASE("Convert::ToBool", "[util][convert]")
{
  bool b = false;
  TEST_ASSERT_TRUE(Convert::ToBool("true", b));
  TEST_ASSERT_TRUE(b);
  TEST_ASSERT_TRUE(Convert::ToBool("false", b));
  TEST_ASSERT_FALSE(b);
  TEST_ASSERT_TRUE(Convert::ToBool("TRUE", b));  // case-insensitive
  TEST_ASSERT_TRUE(b);

  TEST_ASSERT_FALSE(Convert::ToBool("yes", b));
  TEST_ASSERT_FALSE(Convert::ToBool("1", b));
  TEST_ASSERT_FALSE(Convert::ToBool("", b));
}
