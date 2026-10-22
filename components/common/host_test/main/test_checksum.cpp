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
  } p {10, 20, 30, 40};
  TEST_ASSERT_EQUAL_UINT8(100, Checksum::Sum8(p));

  // Usable in constant expressions
  static_assert(Checksum::Sum8(Packed {1, 2, 3, 4}) == 10);
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

TEST_CASE("Crc32 matches the IEEE check value and zlib", "[util][checksum]")
{
  // The standard CRC-32 check value: crc32("123456789") == 0xCBF43926 (also what zlib.crc32 gives).
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, Checksum::Crc32(check, sizeof(check)));
  TEST_ASSERT_EQUAL_HEX32(0x00000000, Checksum::Crc32(check, 0));

  // Chained calls give the same result as one call over the whole span.
  uint32_t partial = Checksum::Crc32(check, 4);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, Checksum::Crc32(check + 4, sizeof(check) - 4, partial));

  // Usable in constant expressions, and the object overload hashes a trivially copyable object's bytes.
  constexpr uint8_t ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  static_assert(Checksum::Crc32(ones, sizeof(ones)) == 0xFFFFFFFF);
  struct FourBytes {
    uint8_t a, b, c, d;
  };
  constexpr FourBytes allOnes {0xFF, 0xFF, 0xFF, 0xFF};
  static_assert(Checksum::Crc32(allOnes) == 0xFFFFFFFF);
  TEST_ASSERT_EQUAL_HEX32(Checksum::Crc32(ones, sizeof(ones)), Checksum::Crc32(allOnes));
}
