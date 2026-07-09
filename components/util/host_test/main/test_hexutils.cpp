// HexUtils: byte<->hex conversions used for BSSID, partition hashes, etc.
#include "unity.h"

#include "util/HexUtils.h"

#include <array>
#include <cstdint>
#include <cstring>

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
  auto out = HexUtils::ToHex<6>(mac);
  TEST_ASSERT_EQUAL_STRING("DEADBEEF0123", out.data());
}

TEST_CASE("TryParseHex round-trips", "[util][hex]")
{
  uint8_t out[3] = {0};
  TEST_ASSERT_EQUAL_size_t(3, HexUtils::TryParseHex("aBcDeF", 6, out, 3));
  TEST_ASSERT_EQUAL_HEX8(0xAB, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, out[2]);

  // odd length / bad chars / small buffer -> 0
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABC", 3, out, 3));    // odd
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABGH", 4, out, 3));   // non-hex
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHex("ABCDEF", 6, out, 2)); // buffer too small
}

TEST_CASE("TryParseHexPair", "[util][hex]")
{
  uint8_t v = 0;
  TEST_ASSERT_TRUE(HexUtils::TryParseHexPair('A', 'b', v));
  TEST_ASSERT_EQUAL_HEX8(0xAB, v);
  TEST_ASSERT_TRUE(HexUtils::TryParseHexPair('0', '0', v));
  TEST_ASSERT_EQUAL_HEX8(0x00, v);
  TEST_ASSERT_FALSE(HexUtils::TryParseHexPair('G', '0', v));  // invalid high nibble
  TEST_ASSERT_FALSE(HexUtils::TryParseHexPair('0', 'Z', v));  // invalid low nibble
}

TEST_CASE("TryParseHexMac", "[util][hex]")
{
  uint8_t out[6] = {0};
  const char* mac = "DE:AD:BE:EF:01:23";
  TEST_ASSERT_EQUAL_size_t(6, HexUtils::TryParseHexMac(mac, std::strlen(mac), out, 6));
  TEST_ASSERT_EQUAL_HEX8(0xDE, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x23, out[5]);

  const char* bad = "DE-AD-BE";  // wrong separator
  TEST_ASSERT_EQUAL_size_t(0, HexUtils::TryParseHexMac(bad, std::strlen(bad), out, 6));
}
