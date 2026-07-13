// Checksum::Sum8 (byte sum) backs the RF protocol frames; ReverseNibble /
// ReverseInverseNibble are lookup-table bit reversals used by some encoders.
#include "unity.h"

#include "Checksum.h"

#include <cstdint>

using namespace OpenShock;

// Independent 4-bit reversal for cross-checking the table-driven versions.
static uint8_t rev4(uint8_t b)
{
  return ((b & 1) << 3) | ((b & 2) << 1) | ((b & 4) >> 1) | ((b & 8) >> 3);
}

TEST_CASE("Sum8 over a byte span wraps at 8 bits", "[util][checksum]")
{
  const uint8_t d[] = {1, 2, 3, 4, 250};  // 260 -> 0x04
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(d, sizeof(d)));
}

TEST_CASE("Sum8 over an integral sums its bytes", "[util][checksum]")
{
  TEST_ASSERT_EQUAL_UINT8(10, Checksum::Sum8(static_cast<uint32_t>(0x01020304)));  // 1+2+3+4
  TEST_ASSERT_EQUAL_UINT8(0xFF, Checksum::Sum8(static_cast<uint8_t>(0xFF)));
  TEST_ASSERT_EQUAL_UINT8(0, Checksum::Sum8(static_cast<uint32_t>(0)));
}

TEST_CASE("Sum8 over a trivially copyable struct", "[util][checksum]")
{
  struct Packed {
    uint8_t a, b, c, d;
  } p{10, 20, 30, 40};
  TEST_ASSERT_EQUAL_UINT8(100, Checksum::Sum8(p));
}

TEST_CASE("ReverseNibble reverses the low nibble bit order", "[util][checksum]")
{
  for (uint8_t b = 0; b < 16; b++) {
    TEST_ASSERT_EQUAL_UINT8(rev4(b), Checksum::ReverseNibble(b));
  }
}

TEST_CASE("ReverseInverseNibble reverses the inverted nibble", "[util][checksum]")
{
  for (uint8_t b = 0; b < 16; b++) {
    TEST_ASSERT_EQUAL_UINT8(rev4((~b) & 0xF), Checksum::ReverseInverseNibble(b));
  }
}
