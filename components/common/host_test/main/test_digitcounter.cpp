// Util::Digits10Count - base-10 digit count (incl. sign) used to size buffers.
#include "unity.h"

#include "util/DigitCounter.h"

#include <cstdint>

using namespace OpenShock;

TEST_CASE("Digits10Count unsigned", "[util][digits]")
{
  TEST_ASSERT_EQUAL_size_t(1, Util::Digits10Count<uint32_t>(0));
  TEST_ASSERT_EQUAL_size_t(1, Util::Digits10Count<uint32_t>(9));
  TEST_ASSERT_EQUAL_size_t(2, Util::Digits10Count<uint32_t>(10));
  TEST_ASSERT_EQUAL_size_t(3, Util::Digits10Count<uint32_t>(999));
  TEST_ASSERT_EQUAL_size_t(4, Util::Digits10Count<uint32_t>(1000));
  TEST_ASSERT_EQUAL_size_t(20, Util::Digits10Count<uint64_t>(UINT64_MAX));
}

TEST_CASE("Digits10Count signed counts the sign", "[util][digits]")
{
  TEST_ASSERT_EQUAL_size_t(1, Util::Digits10Count<int32_t>(0));
  TEST_ASSERT_EQUAL_size_t(1, Util::Digits10Count<int32_t>(7));
  TEST_ASSERT_EQUAL_size_t(2, Util::Digits10Count<int32_t>(-1));          // '-' + '1'
  TEST_ASSERT_EQUAL_size_t(4, Util::Digits10Count<int32_t>(-123));
  TEST_ASSERT_EQUAL_size_t(11, Util::Digits10Count<int32_t>(INT32_MIN));  // -2147483648
}
