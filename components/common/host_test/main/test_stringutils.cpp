// StringHelpers: trimming, prefix/suffix, splitting. All std::string_view based.
#include "unity.h"

#include "StringHelpers.h"

#include <string_view>
#include <vector>

using namespace OpenShock;

TEST_CASE("StringTrim variants", "[common][strhelpers]")
{
  TEST_ASSERT_TRUE(StringTrim("  hi  ") == "hi");
  TEST_ASSERT_TRUE(StringTrimLeft("  hi  ") == "hi  ");
  TEST_ASSERT_TRUE(StringTrimRight("  hi  ") == "  hi");
  TEST_ASSERT_TRUE(StringTrim("") == "");
  TEST_ASSERT_TRUE(StringTrim("   ") == "");
  TEST_ASSERT_TRUE(StringTrim("nospace") == "nospace");
  TEST_ASSERT_TRUE(StringTrim("\t\n x \r\n") == "x");
}

TEST_CASE("StringHasPrefix / HasSuffix", "[common][strhelpers]")
{
  TEST_ASSERT_TRUE(StringHasPrefix("hello", 'h'));
  TEST_ASSERT_FALSE(StringHasPrefix("hello", 'x'));
  TEST_ASSERT_TRUE(StringHasPrefix("hello", std::string_view("hel")));
  TEST_ASSERT_FALSE(StringHasPrefix("hello", std::string_view("world")));
  TEST_ASSERT_FALSE(StringHasPrefix("hi", std::string_view("hello")));  // longer than view
}

TEST_CASE("StringSplitByFirst / ByLast", "[common][strhelpers]")
{
  auto [a, b] = StringSplitByFirst("key=value=extra", '=');
  TEST_ASSERT_TRUE(a == "key");
  TEST_ASSERT_TRUE(b == "value=extra");

  auto [c, d] = StringSplitByLast("a.b.c", '.');
  TEST_ASSERT_TRUE(c == "a.b");
  TEST_ASSERT_TRUE(d == "c");

  // no delimiter -> first half is the whole string
  auto [e, f] = StringSplitByFirst("nodelim", '=');
  TEST_ASSERT_TRUE(e == "nodelim");
}

TEST_CASE("TryStringSplit into a fixed array", "[common][strhelpers]")
{
  std::string_view parts[4];
  TEST_ASSERT_TRUE(TryStringSplit("192.168.0.1", '.', parts));
  TEST_ASSERT_TRUE(parts[0] == "192");
  TEST_ASSERT_TRUE(parts[1] == "168");
  TEST_ASSERT_TRUE(parts[2] == "0");
  TEST_ASSERT_TRUE(parts[3] == "1");

  std::string_view three[4];
  TEST_ASSERT_FALSE(TryStringSplit("1.2.3", '.', three));  // too few -> false

  // "too many": the last slot keeps the whole remainder (nothing dropped), so a
  // caller like IPAddressUtils then rejects it via Convert::ToUint8("4.5").
  std::string_view five[4];
  TEST_ASSERT_TRUE(TryStringSplit("1.2.3.4.5", '.', five));
  TEST_ASSERT_TRUE(five[3] == "4.5");
}

TEST_CASE("StringSplit into a vector", "[common][strhelpers]")
{
  auto v = StringSplit("a,b,,c", ',');
  TEST_ASSERT_EQUAL_size_t(4, v.size());
  TEST_ASSERT_TRUE(v[0] == "a");
  TEST_ASSERT_TRUE(v[2] == "");  // empty field preserved
  TEST_ASSERT_TRUE(v[3] == "c");
}

TEST_CASE("StringIContains / StringHasPrefixIC", "[common][strhelpers]")
{
  TEST_ASSERT_TRUE(StringIContains("Hello World", "LO WO"));
  TEST_ASSERT_FALSE(StringIContains("Hello", "xyz"));
  TEST_ASSERT_TRUE(StringHasPrefixIC("HELLO", "hel"));
  TEST_ASSERT_FALSE(StringHasPrefixIC("hello", "world"));
}
