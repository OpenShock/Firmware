// IPv4Utils: dotted-decimal validation, used to check CONFIG_OPENSHOCK_PORTAL_AP_IP at compile time.
#include "unity.h"

#include "util/IPv4Utils.h"

using namespace OpenShock;

TEST_CASE("IsDottedDecimal accepts valid addresses", "[util][ipv4]")
{
  TEST_ASSERT_TRUE(IPv4Utils::IsDottedDecimal("4.3.2.1"));
  TEST_ASSERT_TRUE(IPv4Utils::IsDottedDecimal("0.0.0.0"));
  TEST_ASSERT_TRUE(IPv4Utils::IsDottedDecimal("255.255.255.255"));
  TEST_ASSERT_TRUE(IPv4Utils::IsDottedDecimal("192.168.10.1"));
}

TEST_CASE("IsDottedDecimal rejects malformed addresses", "[util][ipv4]")
{
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal(""));
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1.2.3"));        // too few octets
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1.2.3.4.5"));    // too many
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1.2.3.4."));     // trailing dot
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal(".1.2.3.4"));     // leading dot
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1..3.4"));       // empty octet
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1.2.3.256"));    // out of range
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1000.2.3.4"));   // too many digits
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("01.2.3.4"));     // leading zero (octal to lwIP)
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("1.2.3.4 "));     // trailing junk
  TEST_ASSERT_FALSE(IPv4Utils::IsDottedDecimal("a.b.c.d"));
}

static_assert(IPv4Utils::IsDottedDecimal("4.3.2.1"));
static_assert(!IPv4Utils::IsDottedDecimal("01.2.3.4"));
