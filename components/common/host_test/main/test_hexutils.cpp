// HexUtils: byte<->hex conversions used for BSSID, partition hashes, etc.
#include "unity.h"

#include "util/HexUtils.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

using namespace OpenShock;

TEST_CASE("ToHex single byte, upper and lower", "[util][hex]")
{
  char buf[3] = {0};
  HexUtils::ToHex(0xAB, buf);
  TEST_ASSERT_EQUAL_STRING("AB", buf);
  HexUtils::ToHex(0x0F, buf);
  TEST_ASSERT_EQUAL_STRING("0F", buf);
  HexUtils::ToHex(0xAB, buf, false);  // lower
  TEST_ASSERT_EQUAL_STRING("ab", buf);
  HexUtils::ToHex(0x00, buf);
  TEST_ASSERT_EQUAL_STRING("00", buf);
}

TEST_CASE("ToHex array -> array", "[util][hex]")
{
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23};
  auto out             = HexUtils::ToHex<6>(mac);
  TEST_ASSERT_EQUAL_STRING("DEADBEEF0123", out.data());
}

TEST_CASE("TryParseHex round-trips", "[util][hex]")
{
  uint8_t out[3] = {0};
  TEST_ASSERT_EQUAL_size_t(3, HexUtils::TryParseHex("aBcDeF", out));
  TEST_ASSERT_EQUAL_HEX8(0xAB, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, out[2]);

  // odd length / bad chars / small buffer -> 0
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABC", out));                         // odd
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABGH", out));                        // non-hex
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABCDEF", std::span(out).first(2)));  // buffer too small
}

TEST_CASE("TryParseHexPair", "[util][hex]")
{
  auto v = HexUtils::TryParseHexPair('A', 'b');
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_HEX8(0xAB, *v);
  v = HexUtils::TryParseHexPair('0', '0');
  TEST_ASSERT_TRUE(v.has_value());  // 0x00 is a valid byte, not a failure
  TEST_ASSERT_EQUAL_HEX8(0x00, *v);
  TEST_ASSERT_FALSE(HexUtils::TryParseHexPair('G', '0').has_value());  // invalid high nibble
  TEST_ASSERT_FALSE(HexUtils::TryParseHexPair('0', 'Z').has_value());  // invalid low nibble

  // Usable in constant expressions
  static_assert(HexUtils::TryParseHexPair('f', 'F') == 0xFF);
}

TEST_CASE("TryParseHexMac", "[util][hex]")
{
  uint8_t out[6]  = {0};
  const char* mac = "DE:AD:BE:EF:01:23";
  TEST_ASSERT_EQUAL_size_t(6, HexUtils::TryParseHexMac(mac, out));
  TEST_ASSERT_EQUAL_HEX8(0xDE, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x23, out[5]);

  const char* bad = "DE-AD-BE";  // wrong separator
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHexMac(bad, out));
}

TEST_CASE("TryParseHexMac edge cases", "[util][hex]")
{
  uint8_t out[6] = {0};

  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHexMac("", out));    // empty
  TEST_ASSERT_EQUAL_size_t(1, HexUtils::TryParseHexMac("AB", out));  // a single pair
  TEST_ASSERT_EQUAL_HEX8(0xAB, out[0]);
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHexMac("DE:AD:BE", std::span(out).first(2)));  // output too small
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHexMac("DE:AD:", out));                        // trailing separator
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHexMac("DE:XY:BE", out));                      // bad hex digit
}

TEST_CASE("TryParseHex only reads the view it is given", "[util][hex]")
{
  // A view into a longer string is not NUL-terminated; only its own characters are parsed
  std::string_view full = "ABCDEFZZ";
  uint8_t out[4]        = {0};
  TEST_ASSERT_EQUAL_size_t(2, HexUtils::TryParseHex(full.substr(0, 4), out));
  TEST_ASSERT_EQUAL_HEX8(0xAB, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, out[1]);
  TEST_ASSERT_EQUAL_size_t(1, HexUtils::TryParseHexMac(std::string_view("12:34", 2), out));
  TEST_ASSERT_EQUAL_HEX8(0x12, out[0]);
}

TEST_CASE("TryParseHex rejects odd lengths, small buffers and bad digits", "[util][hex]")
{
  uint8_t out[4] = {0};

  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABC", out));         // odd length
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("AABBCCDDEE", out));  // output too small
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("AAZZ", out));        // bad digit
  TEST_ASSERT_EQUAL_size_t(2, HexUtils::TryParseHex("aaBB", out));        // mixed case
  TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xBB, out[1]);
}
