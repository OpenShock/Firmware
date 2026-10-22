// StringHelpers::StringIEquals - the shared case-insensitive compare used by the
// enum parsers and util. Length-aware, so it must be correct on non-terminated
// views too.
#include "unity.h"

#include "StringHelpers.h"

#include <string_view>

using namespace OpenShock;

TEST_CASE("StringIEquals: equal regardless of case", "[common][strhelpers]")
{
  TEST_ASSERT_TRUE(StringIEquals("hello", "hello"));
  TEST_ASSERT_TRUE(StringIEquals("Hello", "hELLo"));
  TEST_ASSERT_TRUE(StringIEquals("ABC123", "abc123"));
  TEST_ASSERT_TRUE(StringIEquals("", ""));
}

TEST_CASE("StringIEquals: unequal content or length", "[common][strhelpers]")
{
  TEST_ASSERT_FALSE(StringIEquals("hello", "world"));
  TEST_ASSERT_FALSE(StringIEquals("hello", "hell"));  // different length
  TEST_ASSERT_FALSE(StringIEquals("hell", "hello"));
  TEST_ASSERT_FALSE(StringIEquals("", "x"));
  TEST_ASSERT_FALSE(StringIEquals("x", ""));
  TEST_ASSERT_FALSE(StringIEquals("abc", "abd"));
}

TEST_CASE("StringIEquals: only ASCII letters are folded", "[common][strhelpers]")
{
  // digits/punctuation compare exactly; case folding is A-Z / a-z only
  TEST_ASSERT_TRUE(StringIEquals("a-b_c", "A-B_C"));
  TEST_ASSERT_FALSE(StringIEquals("a-b", "a_b"));
}

TEST_CASE("StringIEquals: works on non-NUL-terminated sub-views", "[common][strhelpers]")
{
  std::string_view full = "prefixHELLOsuffix";
  std::string_view mid  = full.substr(6, 5);  // "HELLO", not NUL-terminated
  TEST_ASSERT_TRUE(StringIEquals(mid, "hello"));
  TEST_ASSERT_FALSE(StringIEquals(mid, "hell"));
}
