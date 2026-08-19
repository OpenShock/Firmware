// TinyVec is the firmware's POD-only dynamic array (manual malloc/realloc growth).
// It underpins buffer assembly across the codebase, so exercise its growth,
// zero-fill, replace and move semantics.
#include "unity.h"

#include "TinyVec.h"

#include <cstdint>
#include <span>
#include <utility>

TEST_CASE("TinyVec append grows and preserves data", "[util][tinyvec]")
{
  TinyVec<uint16_t> v;
  TEST_ASSERT_TRUE(v.empty());

  const uint16_t a[] = {1, 2, 3};
  v.append(a, 3);
  TEST_ASSERT_EQUAL_UINT32(3, v.size());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3, v.capacity());

  const uint16_t b[] = {4, 5};
  v.append(b, 2);
  TEST_ASSERT_EQUAL_UINT32(5, v.size());
  for (uint16_t i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_UINT16(i + 1, v[i]);
  }
}

TEST_CASE("TinyVec resize zero-fills the grown region", "[util][tinyvec]")
{
  TinyVec<uint8_t> v;
  const uint8_t a[] = {0xAA, 0xBB};
  v.append(a, 2);

  v.resize(5);
  TEST_ASSERT_EQUAL_UINT32(5, v.size());
  TEST_ASSERT_EQUAL_UINT8(0xAA, v[0]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, v[1]);
  TEST_ASSERT_EQUAL_UINT8(0, v[2]);
  TEST_ASSERT_EQUAL_UINT8(0, v[4]);
}

TEST_CASE("TinyVec assign replaces the contents", "[util][tinyvec]")
{
  TinyVec<int> v;
  const int a[] = {7, 8, 9, 10};
  v.append(a, 4);

  const int b[] = {1, 2};
  v.assign(b, 2);
  TEST_ASSERT_EQUAL_UINT32(2, v.size());
  TEST_ASSERT_EQUAL_INT(1, v[0]);
  TEST_ASSERT_EQUAL_INT(2, v[1]);

  v.clear();
  TEST_ASSERT_TRUE(v.empty());
}

TEST_CASE("TinyVec move transfers ownership", "[util][tinyvec]")
{
  TinyVec<uint32_t> v;
  const uint32_t a[] = {100, 200};
  v.append(a, 2);

  TinyVec<uint32_t> w(std::move(v));
  TEST_ASSERT_EQUAL_UINT32(2, w.size());
  TEST_ASSERT_EQUAL_UINT32(100, w[0]);
  TEST_ASSERT_EQUAL_UINT32(0, v.size());  // moved-from is emptied
}

TEST_CASE("TinyVec constructs from a buffer and converts to span", "[util][tinyvec]")
{
  const uint8_t a[] = {5, 6, 7};
  TinyVec<uint8_t> v(a, 3);
  TEST_ASSERT_EQUAL_UINT32(3, v.size());
  TEST_ASSERT_EQUAL_UINT8(6, v[1]);

  std::span<const uint8_t> s = v;
  TEST_ASSERT_EQUAL_UINT32(3, s.size());
  TEST_ASSERT_EQUAL_UINT8(7, s[2]);
}
