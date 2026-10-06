// Frame layout tests for the Petrainer, Petrainer 998DR, Wellturn T330 and D80 encoders. Each test decodes the
// emitted RMT symbols back into bits and checks every field of the documented payload layout, so a refactor of the
// encoders (or of EncodeBits) can't silently change what goes on air.
#include "unity.h"

#include "Checksum.h"
#include "radio/rmt/D80Encoder.h"
#include "radio/rmt/Petrainer998DREncoder.h"
#include "radio/rmt/PetrainerEncoder.h"
#include "radio/rmt/T330Encoder.h"

#include <cstdint>

using namespace OpenShock;

// Decode `n` data symbols (MSB first); `isOne` tells a one symbol from a zero symbol for the protocol.
template<typename IsOne>
static uint64_t decode(const rmt_symbol_word_t* seq, size_t n, IsOne isOne)
{
  uint64_t v = 0;
  for (size_t i = 0; i < n; i++) {
    v = (v << 1) | (isOne(seq[i]) ? 1U : 0U);
  }
  return v;
}

// ---------------------------------------------------------------------------
// Petrainer: [preamble][type:8][shockerId:16][intensity:8][typeSum:8][postamble]
// type = 0x80 | (1 << n), typeSum = ~(0x01 | (0x80 >> n)), n = 0/1/2 for shock/vibrate/sound
// ---------------------------------------------------------------------------
static bool petrainerOne(const rmt_symbol_word_t& s)
{
  return s.duration1 > 1000;  // one = 200/1500, zero = 200/750
}

TEST_CASE("Petrainer frame layout", "[protocols][petrainer]")
{
  TEST_ASSERT_EQUAL(42, Rmt::PetrainerEncoder::GetBufferSize());

  rmt_symbol_word_t seq[42];
  TEST_ASSERT_TRUE(Rmt::PetrainerEncoder::FillBuffer(seq, 0x1234, ShockerCommandType::Shock, 42));

  TEST_ASSERT_EQUAL_UINT16(750, seq[0].duration0);    // preamble
  TEST_ASSERT_EQUAL_UINT16(7000, seq[41].duration1);  // postamble

  const uint64_t data = decode(seq + 1, 40, petrainerOne);
  TEST_ASSERT_EQUAL_UINT8(0x81, (data >> 32) & 0xFF);  // 0x80 | 1<<0
  TEST_ASSERT_EQUAL_UINT16(0x1234, (data >> 16) & 0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(42, (data >> 8) & 0xFF);
  TEST_ASSERT_EQUAL_UINT8(0x7E, data & 0xFF);  // ~(0x01 | 0x80)
}

TEST_CASE("Petrainer type codes, clamping and unsupported types", "[protocols][petrainer]")
{
  rmt_symbol_word_t seq[42];

  TEST_ASSERT_TRUE(Rmt::PetrainerEncoder::FillBuffer(seq, 1, ShockerCommandType::Vibrate, 200));
  uint64_t data = decode(seq + 1, 40, petrainerOne);
  TEST_ASSERT_EQUAL_UINT8(0x82, (data >> 32) & 0xFF);  // 0x80 | 1<<1
  TEST_ASSERT_EQUAL_UINT8(100, (data >> 8) & 0xFF);    // clamped
  TEST_ASSERT_EQUAL_UINT8(0xBE, data & 0xFF);          // ~(0x01 | 0x40)

  TEST_ASSERT_TRUE(Rmt::PetrainerEncoder::FillBuffer(seq, 1, ShockerCommandType::Sound, 10));
  data = decode(seq + 1, 40, petrainerOne);
  TEST_ASSERT_EQUAL_UINT8(0x84, (data >> 32) & 0xFF);  // 0x80 | 1<<2
  TEST_ASSERT_EQUAL_UINT8(0xDE, data & 0xFF);          // ~(0x01 | 0x20)

  TEST_ASSERT_FALSE(Rmt::PetrainerEncoder::FillBuffer(seq, 1, ShockerCommandType::Light, 10));
  TEST_ASSERT_FALSE(Rmt::PetrainerEncoder::FillBuffer(seq, 1, ShockerCommandType::Stop, 10));
}

// ---------------------------------------------------------------------------
// Petrainer 998DR: [preamble][channel:4][type:4][shockerId:16][intensity:8][typeInvert:4][channelInvert:4][postamble]
// channel = 0b1000 (CH1); *Invert = bit-reversed, inverted nibble
// ---------------------------------------------------------------------------
static bool p998drOne(const rmt_symbol_word_t& s)
{
  return s.duration0 > 500;  // one = 750/250, zero = 250/750
}

TEST_CASE("Petrainer 998DR frame layout", "[protocols][998dr]")
{
  TEST_ASSERT_EQUAL(42, Rmt::Petrainer998DREncoder::GetBufferSize());

  rmt_symbol_word_t seq[42];
  TEST_ASSERT_TRUE(Rmt::Petrainer998DREncoder::FillBuffer(seq, 0xBEEF, ShockerCommandType::Shock, 55));

  TEST_ASSERT_EQUAL_UINT16(1500, seq[0].duration0);   // preamble
  TEST_ASSERT_EQUAL_UINT16(3750, seq[41].duration1);  // postamble

  const uint64_t data = decode(seq + 1, 40, p998drOne);
  TEST_ASSERT_EQUAL_UINT8(0b1000, (data >> 36) & 0xF);  // channel
  TEST_ASSERT_EQUAL_UINT8(0b0001, (data >> 32) & 0xF);  // shock
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, (data >> 16) & 0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(55, (data >> 8) & 0xFF);
  TEST_ASSERT_EQUAL_UINT8(0b0111, (data >> 4) & 0xF);  // reverse(0001)=1000, inverted
  TEST_ASSERT_EQUAL_UINT8(0b1110, data & 0xF);         // reverse(1000)=0001, inverted
}

TEST_CASE("Petrainer 998DR type codes and clamping", "[protocols][998dr]")
{
  rmt_symbol_word_t seq[42];

  struct Case {
    ShockerCommandType type;
    uint8_t typeVal;
    uint8_t typeInvert;
  };
  const Case cases[] = {
    {ShockerCommandType::Vibrate, 0b0010, 0b1011},
    {  ShockerCommandType::Sound, 0b0100, 0b1101},
    {  ShockerCommandType::Light, 0b1000, 0b1110},
  };
  for (const Case& c : cases) {
    TEST_ASSERT_TRUE(Rmt::Petrainer998DREncoder::FillBuffer(seq, 1, c.type, 250));
    const uint64_t data = decode(seq + 1, 40, p998drOne);
    TEST_ASSERT_EQUAL_UINT8(c.typeVal, (data >> 32) & 0xF);
    TEST_ASSERT_EQUAL_UINT8(100, (data >> 8) & 0xFF);  // clamped
    TEST_ASSERT_EQUAL_UINT8(c.typeInvert, (data >> 4) & 0xF);
  }

  TEST_ASSERT_FALSE(Rmt::Petrainer998DREncoder::FillBuffer(seq, 1, ShockerCommandType::Stop, 10));
}

// ---------------------------------------------------------------------------
// Wellturn T330: [preamble][channel:4][typeHigh:4][shockerId:16][intensity:8][typeLow:4][channel:4][0][postamble]
// ---------------------------------------------------------------------------
static bool t330One(const rmt_symbol_word_t& s)
{
  return s.duration1 > 800;  // one = 220/980, zero = 220/580
}

TEST_CASE("Wellturn T330 frame layout", "[protocols][t330]")
{
  TEST_ASSERT_EQUAL(43, Rmt::WellturnT330Encoder::GetBufferSize());

  rmt_symbol_word_t seq[43];
  TEST_ASSERT_TRUE(Rmt::WellturnT330Encoder::FillBuffer(seq, 0x0F0F, ShockerCommandType::Vibrate, 77));

  TEST_ASSERT_EQUAL_UINT16(960, seq[0].duration0);   // preamble
  TEST_ASSERT_EQUAL_UINT16(135, seq[42].duration1);  // postamble

  const uint64_t bits = decode(seq + 1, 41, t330One);
  TEST_ASSERT_EQUAL_UINT8(0, bits & 1);              // trailing zero bit
  const uint64_t data = bits >> 1;
  TEST_ASSERT_EQUAL_UINT8(0, (data >> 36) & 0xF);    // channel
  TEST_ASSERT_EQUAL_UINT8(0x7, (data >> 32) & 0xF);  // vibrate 0b0111'0010, high nibble
  TEST_ASSERT_EQUAL_UINT16(0x0F0F, (data >> 16) & 0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(77, (data >> 8) & 0xFF);
  TEST_ASSERT_EQUAL_UINT8(0x2, (data >> 4) & 0xF);  // low nibble
  TEST_ASSERT_EQUAL_UINT8(0, data & 0xF);           // channel again
}

TEST_CASE("Wellturn T330 shock/sound codes, clamping and unsupported types", "[protocols][t330]")
{
  rmt_symbol_word_t seq[43];

  TEST_ASSERT_TRUE(Rmt::WellturnT330Encoder::FillBuffer(seq, 1, ShockerCommandType::Shock, 150));
  uint64_t data = decode(seq + 1, 41, t330One) >> 1;
  TEST_ASSERT_EQUAL_UINT8(0x6, (data >> 32) & 0xF);  // 0b0110'0001
  TEST_ASSERT_EQUAL_UINT8(0x1, (data >> 4) & 0xF);
  TEST_ASSERT_EQUAL_UINT8(100, (data >> 8) & 0xFF);  // clamped

  TEST_ASSERT_TRUE(Rmt::WellturnT330Encoder::FillBuffer(seq, 1, ShockerCommandType::Sound, 50));
  data = decode(seq + 1, 41, t330One) >> 1;
  TEST_ASSERT_EQUAL_UINT8(0x8, (data >> 32) & 0xF);  // 0b1000'0100
  TEST_ASSERT_EQUAL_UINT8(0x4, (data >> 4) & 0xF);
  TEST_ASSERT_EQUAL_UINT8(0, (data >> 8) & 0xFF);    // sound always sends 0

  TEST_ASSERT_FALSE(Rmt::WellturnT330Encoder::FillBuffer(seq, 1, ShockerCommandType::Light, 10));
}

// ---------------------------------------------------------------------------
// D80: [preamble][0x04:8][shockerId:16][type:2][channel:2][intensity:4][checksum:8][postamble]
// intensity is scaled 0-100 -> 0-15 (non-zero never rounds down to 0); channel = 1
// ---------------------------------------------------------------------------
static bool d80One(const rmt_symbol_word_t& s)
{
  return s.duration0 > 600;  // one = 900/300, zero = 300/900
}

static uint32_t d80Payload(const rmt_symbol_word_t* seq, uint8_t* checksumOut)
{
  const uint64_t data = decode(seq + 1, 40, d80One);
  *checksumOut        = data & 0xFF;
  return static_cast<uint32_t>(data >> 8);
}

TEST_CASE("D80 frame layout and checksum", "[protocols][d80]")
{
  TEST_ASSERT_EQUAL(42, Rmt::D80Encoder::GetBufferSize());

  rmt_symbol_word_t seq[42];
  TEST_ASSERT_TRUE(Rmt::D80Encoder::FillBuffer(seq, 0x5A5A, ShockerCommandType::Shock, 100));

  TEST_ASSERT_EQUAL_UINT16(1900, seq[0].duration0);   // preamble
  TEST_ASSERT_EQUAL_UINT16(2200, seq[41].duration1);  // postamble

  uint8_t checksum;
  const uint32_t payload = d80Payload(seq, &checksum);
  TEST_ASSERT_EQUAL_UINT8(0x04, (payload >> 24) & 0xFF);
  TEST_ASSERT_EQUAL_UINT16(0x5A5A, (payload >> 8) & 0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(0x1, (payload >> 6) & 0x3);  // shock
  TEST_ASSERT_EQUAL_UINT8(0x1, (payload >> 4) & 0x3);  // channel
  TEST_ASSERT_EQUAL_UINT8(15, payload & 0xF);          // 100% -> 15
  TEST_ASSERT_EQUAL_UINT8(Checksum::Sum8(payload), checksum);
}

TEST_CASE("D80 intensity scaling and clamping", "[protocols][d80]")
{
  rmt_symbol_word_t seq[42];
  uint8_t checksum;

  struct Case {
    uint8_t in;
    uint8_t out;
  };
  const Case cases[] = {
    {  0,  0},
    {  1,  1}, // never rounds a non-zero intensity down to off
    { 50,  7},
    {100, 15},
    {107, 15}, // clamped (used to wrap to 0)
    {255, 15}, // clamped (used to wrap to 6)
  };
  for (const Case& c : cases) {
    TEST_ASSERT_TRUE(Rmt::D80Encoder::FillBuffer(seq, 1, ShockerCommandType::Vibrate, c.in));
    const uint32_t payload = d80Payload(seq, &checksum);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.out, payload & 0xF, "intensity");
    TEST_ASSERT_EQUAL_UINT8(0x2, (payload >> 6) & 0x3);  // vibrate
  }
}

TEST_CASE("D80 sound sends zero intensity; unsupported types are rejected", "[protocols][d80]")
{
  rmt_symbol_word_t seq[42];
  uint8_t checksum;

  TEST_ASSERT_TRUE(Rmt::D80Encoder::FillBuffer(seq, 1, ShockerCommandType::Sound, 80));
  const uint32_t payload = d80Payload(seq, &checksum);
  TEST_ASSERT_EQUAL_UINT8(0x3, (payload >> 6) & 0x3);
  TEST_ASSERT_EQUAL_UINT8(0, payload & 0xF);

  TEST_ASSERT_FALSE(Rmt::D80Encoder::FillBuffer(seq, 1, ShockerCommandType::Light, 10));
  TEST_ASSERT_FALSE(Rmt::D80Encoder::FillBuffer(seq, 1, ShockerCommandType::Stop, 10));
}
