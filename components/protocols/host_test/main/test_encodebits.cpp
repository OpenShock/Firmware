// EncodeBits<N> is the shared primitive behind every RF encoder: it maps the N
// most-significant meaningful bits of an integer to RMT one/zero symbols,
// MSB-first. A regression here silently corrupts every shocker command.
#include "unity.h"

#include "radio/rmt/internal/Shared.h"

#include <cstdint>

using OpenShock::Rmt::Internal::EncodeBits;

// Distinct one/zero symbols; only duration0 is inspected to classify a bit.
static const rmt_symbol_word_t ONE  = {750, 1, 250, 0};
static const rmt_symbol_word_t ZERO = {250, 1, 750, 0};

static bool isOne(const rmt_symbol_word_t& s)
{
  return s.duration0 == ONE.duration0;
}

TEST_CASE("EncodeBits emits all N bits MSB-first", "[protocols][encodebits]")
{
  rmt_symbol_word_t seq[8];
  EncodeBits<8>(seq, static_cast<uint8_t>(0b10110010), ONE, ZERO);

  const bool expected[8] = {1, 0, 1, 1, 0, 0, 1, 0};
  for (int i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL(expected[i], isOne(seq[i]));
  }
}

TEST_CASE("EncodeBits<N> takes the low N bits, MSB-first", "[protocols][encodebits]")
{
  // N=4 over a uint8: only the low nibble is transmitted, top nibble ignored.
  rmt_symbol_word_t seq[4];
  EncodeBits<4>(seq, static_cast<uint8_t>(0b1111'1010), ONE, ZERO);

  const bool expected[4] = {1, 0, 1, 0};
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL(expected[i], isOne(seq[i]));
  }
}

TEST_CASE("EncodeBits selects the correct symbol object", "[protocols][encodebits]")
{
  rmt_symbol_word_t seq[2];
  EncodeBits<2>(seq, static_cast<uint8_t>(0b10), ONE, ZERO);

  // Bit 1 -> ONE symbol verbatim, bit 0 -> ZERO symbol verbatim.
  TEST_ASSERT_EQUAL_UINT32(ONE.val, seq[0].val);
  TEST_ASSERT_EQUAL_UINT32(ZERO.val, seq[1].val);
}
