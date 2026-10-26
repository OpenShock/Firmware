#include "unity.h"

#include "RetainedRing.h"

#include <cstring>
#include <string>

using namespace OpenShock::Diagnostics;

namespace {
  struct Ring {
    RetainedRingState state {};
    char data[16] {};
    RetainedRing ring {state, data, sizeof(data)};

    Ring() { ring.Reset(); }

    std::string Peek(uint64_t start, uint64_t& from) const
    {
      char out[64];
      std::size_t len = ring.Peek(start, out, sizeof(out), from);
      return std::string(out, len);
    }
  };
}  // namespace

TEST_CASE("RetainedRing keeps appended bytes until consumed", "[retained_ring]")
{
  Ring r;
  r.ring.Append("hello ", 6);
  r.ring.Append("world", 5);

  uint64_t from = 0;
  TEST_ASSERT_EQUAL_STRING("hello world", r.Peek(r.ring.Consumed(), from).c_str());
  TEST_ASSERT_EQUAL_UINT64(0, from);
  TEST_ASSERT_EQUAL_UINT64(11, r.ring.Pending());

  r.ring.Consume(6);
  TEST_ASSERT_EQUAL_STRING("world", r.Peek(r.ring.Consumed(), from).c_str());
  TEST_ASSERT_EQUAL_UINT64(6, from);
  TEST_ASSERT_EQUAL_UINT64(5, r.ring.Pending());
}

TEST_CASE("RetainedRing overwrites its oldest bytes and reports where the survivors start", "[retained_ring]")
{
  Ring r;
  r.ring.Append("0123456789", 10);
  r.ring.Append("abcdefghij", 10);  // 20 bytes into 16: the first 4 are gone

  uint64_t from   = 0;
  std::string got = r.Peek(r.ring.Consumed(), from);
  TEST_ASSERT_EQUAL_UINT64(4, from);
  TEST_ASSERT_EQUAL_STRING("456789abcdefghij", got.c_str());
  TEST_ASSERT_EQUAL_UINT64(20, r.ring.Pending());  // Pending counts the lost bytes too
}

TEST_CASE("RetainedRing keeps the tail of an append longer than itself", "[retained_ring]")
{
  Ring r;
  r.ring.Append("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26);

  uint64_t from = 0;
  TEST_ASSERT_EQUAL_STRING("KLMNOPQRSTUVWXYZ", r.Peek(0, from).c_str());
  TEST_ASSERT_EQUAL_UINT64(10, from);
}

TEST_CASE("RetainedRing peeks from a position past the consumed one", "[retained_ring]")
{
  Ring r;
  r.ring.Append("abcdef", 6);

  uint64_t from = 0;
  TEST_ASSERT_EQUAL_STRING("def", r.Peek(3, from).c_str());
  TEST_ASSERT_EQUAL_UINT64(3, from);
  TEST_ASSERT_EQUAL_STRING("", r.Peek(6, from).c_str());
}

TEST_CASE("RetainedRing clamps Consume to what was written and never moves back", "[retained_ring]")
{
  Ring r;
  r.ring.Append("abc", 3);
  r.ring.Consume(100);
  TEST_ASSERT_EQUAL_UINT64(3, r.ring.Consumed());
  r.ring.Consume(1);
  TEST_ASSERT_EQUAL_UINT64(3, r.ring.Consumed());
}

TEST_CASE("RetainedRing adopts intact state and resets damaged state", "[retained_ring]")
{
  Ring r;
  r.ring.Append("abc", 3);

  // A second view over the same memory, as after a software reset.
  RetainedRing again(r.state, r.data, sizeof(r.data));
  TEST_ASSERT_TRUE(again.Adopt());
  TEST_ASSERT_EQUAL_UINT64(3, again.Pending());

  r.state.written ^= 1;  // Stale memory after a power-up
  TEST_ASSERT_FALSE(again.Adopt());
  TEST_ASSERT_EQUAL_UINT64(0, again.Pending());

  RetainedRing otherSize(r.state, r.data, sizeof(r.data) - 1);
  TEST_ASSERT_FALSE(otherSize.Adopt());
}
