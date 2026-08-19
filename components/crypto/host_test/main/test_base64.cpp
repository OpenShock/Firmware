// Base64 encode/decode (mbedtls-backed) - used for auth tokens etc.
#include "unity.h"

#include "TinyVec.h"
#include "Base64.h"

#include <cstdint>
#include <span>
#include <string>

using namespace OpenShock;

TEST_CASE("Base64 encode known vectors", "[util][base64]")
{
  std::string out;

  const uint8_t man[3] = {'M', 'a', 'n'};
  TEST_ASSERT_TRUE(Base64::Encode(std::span<const uint8_t>(man, 3), out));
  TEST_ASSERT_TRUE(out == "TWFu");

  // padding cases
  const uint8_t m[1] = {'M'};
  TEST_ASSERT_TRUE(Base64::Encode(std::span<const uint8_t>(m, 1), out));
  TEST_ASSERT_TRUE(out == "TQ==");
  const uint8_t ma[2] = {'M', 'a'};
  TEST_ASSERT_TRUE(Base64::Encode(std::span<const uint8_t>(ma, 2), out));
  TEST_ASSERT_TRUE(out == "TWE=");
}

TEST_CASE("Base64 decode known vector", "[util][base64]")
{
  TinyVec<uint8_t> out;
  TEST_ASSERT_TRUE(Base64::Decode("TWFu", out));
  TEST_ASSERT_EQUAL_size_t(3, out.size());
  TEST_ASSERT_EQUAL_UINT8('M', out[0]);
  TEST_ASSERT_EQUAL_UINT8('a', out[1]);
  TEST_ASSERT_EQUAL_UINT8('n', out[2]);
}

TEST_CASE("Base64 decode rejects invalid input", "[util][base64]")
{
  TinyVec<uint8_t> out;
  TEST_ASSERT_FALSE(Base64::Decode("!!!!", out));  // invalid chars
}

TEST_CASE("Base64 round-trip", "[util][base64]")
{
  const uint8_t data[5] = {0x00, 0xFF, 0x10, 0x80, 0x7F};
  std::string encoded;
  TEST_ASSERT_TRUE(Base64::Encode(std::span<const uint8_t>(data, 5), encoded));

  TinyVec<uint8_t> decoded;
  TEST_ASSERT_TRUE(Base64::Decode(encoded, decoded));
  TEST_ASSERT_EQUAL_size_t(5, decoded.size());
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_UINT8(data[i], decoded[i]);
  }
}
