// Checksum::Sum8 (byte sum) backs the RF protocol frames; ReverseNibble /
// ReverseInverseNibble are lookup-table bit reversals used by some encoders.
#include "unity.h"

#include "Checksum.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

using namespace OpenShock;

// Independent 4-bit reversal for cross-checking the table-driven versions.
static uint8_t rev4(uint8_t b)
{
  return ((b & 1) << 3) | ((b & 2) << 1) | ((b & 4) >> 1) | ((b & 8) >> 3);
}

TEST_CASE("Sum8 over a byte span wraps at 8 bits", "[util][checksum]")
{
  const uint8_t d[] = {1, 2, 3, 4, 250};  // 260 -> 0x04
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(std::span<const uint8_t>(d)));
  TEST_ASSERT_EQUAL_UINT8(0, Checksum::Sum8(std::span<const uint8_t>()));
}

TEST_CASE("Sum8 picks the span overload for byte containers and spans", "[util][checksum]")
{
  // Every one of these must sum the bytes they hold or point at; the object overload would instead sum the
  // container's own representation (for a span: its pointer and size).
  uint8_t raw[]                      = {1, 2, 3, 4, 250};
  std::array<uint8_t, 5> arr         = {1, 2, 3, 4, 250};
  std::vector<uint8_t> vec           = {1, 2, 3, 4, 250};
  std::span<uint8_t> mutableSpan     = raw;
  std::span<const uint8_t> constSpan = raw;
  std::span<uint8_t, 5> fixedSpan    = raw;

  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(raw));
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(arr));
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(vec));
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(mutableSpan));
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(constSpan));
  TEST_ASSERT_EQUAL_UINT8(4, Checksum::Sum8(fixedSpan));
  TEST_ASSERT_EQUAL_UINT8(3, Checksum::Sum8(mutableSpan.first(2)));  // only the viewed bytes

  // The same goes for Crc32
  TEST_ASSERT_EQUAL_HEX32(Checksum::Crc32(constSpan), Checksum::Crc32(mutableSpan));
  TEST_ASSERT_EQUAL_HEX32(Checksum::Crc32(constSpan), Checksum::Crc32(arr));
  TEST_ASSERT_EQUAL_HEX32(Checksum::Crc32(constSpan), Checksum::Crc32(raw));
}

// The shift-based byte sum the integral overload used to compute, to show the object-representation sum agrees.
template<typename T>
static uint8_t shiftSum8(T value)
{
  auto bits      = static_cast<std::make_unsigned_t<T>>(value);
  uint8_t result = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    result += static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
  }
  return result;
}

TEST_CASE("Sum8 over an integral sums its bytes", "[util][checksum]")
{
  TEST_ASSERT_EQUAL_UINT8(10, Checksum::Sum8(static_cast<uint32_t>(0x01020304)));  // 1+2+3+4
  TEST_ASSERT_EQUAL_UINT8(0xFF, Checksum::Sum8(static_cast<uint8_t>(0xFF)));
  TEST_ASSERT_EQUAL_UINT8(0, Checksum::Sum8(static_cast<uint32_t>(0)));

  // Usable in constant expressions
  static_assert(Checksum::Sum8(static_cast<uint16_t>(0x0102)) == 3);
}

TEST_CASE("Sum8 over an integral equals the shift-based byte sum", "[util][checksum]")
{
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<uint16_t>(0xBEEF), Checksum::Sum8(static_cast<uint16_t>(0xBEEF)));
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<uint32_t>(0xDEADBEEF), Checksum::Sum8(static_cast<uint32_t>(0xDEADBEEF)));
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<uint64_t>(0x0123456789ABCDEFull), Checksum::Sum8(static_cast<uint64_t>(0x0123456789ABCDEFull)));

  // Negative values sum their two's complement bytes
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<int8_t>(-1), Checksum::Sum8(static_cast<int8_t>(-1)));
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<int16_t>(-2), Checksum::Sum8(static_cast<int16_t>(-2)));
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<int32_t>(INT32_MIN), Checksum::Sum8(static_cast<int32_t>(INT32_MIN)));
  TEST_ASSERT_EQUAL_UINT8(shiftSum8<int64_t>(-1234567890123ll), Checksum::Sum8(static_cast<int64_t>(-1234567890123ll)));

  // A sweep over every 16-bit value
  for (uint32_t v = 0; v <= 0xFFFF; ++v) {
    TEST_ASSERT_EQUAL_UINT8(shiftSum8<uint16_t>(static_cast<uint16_t>(v)), Checksum::Sum8(static_cast<uint16_t>(v)));
  }
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
  const uint8_t check[]                    = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  const std::span<const uint8_t> checkSpan = check;
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, Checksum::Crc32(checkSpan));
  TEST_ASSERT_EQUAL_HEX32(0x00000000, Checksum::Crc32(checkSpan.first(0)));

  // Chained calls give the same result as one call over the whole span.
  uint32_t partial = Checksum::Crc32(checkSpan.first(4));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, Checksum::Crc32(checkSpan.subspan(4), partial));

  // Usable in constant expressions, and the object overload hashes a trivially copyable object's bytes.
  static constexpr uint8_t ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  static_assert(Checksum::Crc32(ones) == 0xFFFFFFFF);
  struct FourBytes {
    uint8_t a, b, c, d;
  };
  constexpr FourBytes allOnes {0xFF, 0xFF, 0xFF, 0xFF};
  static_assert(Checksum::Crc32(allOnes) == 0xFFFFFFFF);
  TEST_ASSERT_EQUAL_HEX32(Checksum::Crc32(ones), Checksum::Crc32(allOnes));
}
