// CaiXianlin frame assembly: [preamble][shockerId:16][channel:4][type:4]
// [intensity:8][checksum:8][postamble:3]. These tests decode the emitted RMT
// symbols back to bits and check the layout, the command-type codes, intensity
// clamping, and the Sound-mutes-intensity quirk.
#include "unity.h"

#include "radio/rmt/CaiXianlinEncoder.h"
#include "Checksum.h"

#include <cstdint>

using namespace OpenShock;

static bool bitOf(const rmt_symbol_word_t& s)
{
  // One vs zero symbols differ by the leading pulse width (750 vs 250).
  return s.duration0 > 500;
}

// Decode n data symbols (MSB-first) back into an integer.
static uint64_t decode(const rmt_symbol_word_t* seq, size_t n)
{
  uint64_t v = 0;
  for (size_t i = 0; i < n; i++) {
    v = (v << 1) | (bitOf(seq[i]) ? 1U : 0U);
  }
  return v;
}

TEST_CASE("CaiXianlin buffer is 44 symbols", "[protocols][caixianlin]")
{
  TEST_ASSERT_EQUAL(44, Rmt::CaiXianlinEncoder::GetBufferSize());
}

TEST_CASE("CaiXianlin rejects unsupported command types", "[protocols][caixianlin]")
{
  rmt_symbol_word_t seq[44];
  TEST_ASSERT_FALSE(Rmt::CaiXianlinEncoder::FillBuffer(seq, 0x1234, 5, ShockerCommandType::Stop, 42));
}

TEST_CASE("CaiXianlin encodes id/channel/type/intensity + checksum", "[protocols][caixianlin]")
{
  const uint16_t id        = 0x1234;
  const uint8_t channel    = 5;
  const uint8_t intensity  = 42;

  rmt_symbol_word_t seq[44];
  TEST_ASSERT_TRUE(Rmt::CaiXianlinEncoder::FillBuffer(seq, id, channel, ShockerCommandType::Shock, intensity));

  // Symbol 0 is the preamble (1400us high / 750us low).
  TEST_ASSERT_EQUAL_UINT16(1400, seq[0].duration0);
  TEST_ASSERT_EQUAL_UINT16(1, seq[0].level0);
  TEST_ASSERT_EQUAL_UINT16(750, seq[0].duration1);

  // Symbols 1..43 carry 43 data bits: (payload<<8 | checksum) << 3.
  const uint64_t tx = decode(seq + 1, 43);
  TEST_ASSERT_EQUAL_UINT32(0, tx & 0x7);  // 3-bit zero postamble

  const uint64_t frame    = tx >> 3;             // payload<<8 | checksum
  const uint8_t  checksum = frame & 0xFF;
  const uint32_t payload  = static_cast<uint32_t>(frame >> 8);

  TEST_ASSERT_EQUAL_UINT16(id, (payload >> 16) & 0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(channel, (payload >> 12) & 0xF);
  TEST_ASSERT_EQUAL_UINT8(0x01, (payload >> 8) & 0xF);  // Shock -> 0x01
  TEST_ASSERT_EQUAL_UINT8(intensity, payload & 0xFF);
  TEST_ASSERT_EQUAL_UINT8(Checksum::Sum8(payload), checksum);
}

TEST_CASE("CaiXianlin clamps intensity to 99", "[protocols][caixianlin]")
{
  rmt_symbol_word_t seq[44];
  TEST_ASSERT_TRUE(Rmt::CaiXianlinEncoder::FillBuffer(seq, 1, 0, ShockerCommandType::Vibrate, 200));

  const uint32_t payload = static_cast<uint32_t>((decode(seq + 1, 43) >> 3) >> 8);
  TEST_ASSERT_EQUAL_UINT8(99, payload & 0xFF);
  TEST_ASSERT_EQUAL_UINT8(0x02, (payload >> 8) & 0xF);  // Vibrate -> 0x02
}

TEST_CASE("CaiXianlin forces Sound intensity to zero", "[protocols][caixianlin]")
{
  rmt_symbol_word_t seq[44];
  TEST_ASSERT_TRUE(Rmt::CaiXianlinEncoder::FillBuffer(seq, 1, 0, ShockerCommandType::Sound, 55));

  const uint32_t payload = static_cast<uint32_t>((decode(seq + 1, 43) >> 3) >> 8);
  TEST_ASSERT_EQUAL_UINT8(0x03, (payload >> 8) & 0xF);  // Sound -> 0x03
  TEST_ASSERT_EQUAL_UINT8(0, payload & 0xFF);           // intensity muted
}
