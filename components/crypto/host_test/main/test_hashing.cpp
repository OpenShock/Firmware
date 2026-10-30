// The MD5 hasher against RFC 1321 known answers, through both the one-shot and the incremental API.
#include "unity.h"

#include "Hashing.h"

#include <cstdint>
#include <string>

using namespace OpenShock;

namespace {
  void ExpectMd5(std::string_view input, const MD5::Digest& expected)
  {
    const auto* data = reinterpret_cast<const uint8_t*>(input.data());

    MD5::Digest digest {};
    TEST_ASSERT_TRUE(MD5::compute(data, input.size(), digest));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), digest.data(), digest.size());

    digest = {};
    MD5 md5;
    TEST_ASSERT_TRUE(md5.begin());
    TEST_ASSERT_TRUE(md5.update(input));
    TEST_ASSERT_TRUE(md5.finish(digest));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), digest.data(), digest.size());
  }
}  // namespace

TEST_CASE("MD5 matches the RFC 1321 test suite", "[crypto][md5]")
{
  ExpectMd5("", {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e});
  ExpectMd5("The quick brown fox jumps over the lazy dog", {0x9e, 0x10, 0x7d, 0x9d, 0x37, 0x2b, 0xb6, 0x82, 0x6b, 0xd8, 0x1d, 0x35, 0x42, 0xa4, 0x19, 0xd6});
  // crosses a block, length lands in the next padding block
  ExpectMd5(std::string(80, 'a'), {0xb1, 0x5a, 0xf9, 0xcd, 0xab, 0xba, 0xea, 0x05, 0x16, 0x86, 0x6a, 0x33, 0xd8, 0xfd, 0x0f, 0x98});
}
