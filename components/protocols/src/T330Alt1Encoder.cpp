#include "radio/rmt/T330Alt1Encoder.h"

#include "radio/rmt/internal/Shared.h"

#include <algorithm>
#include <unordered_map>

const rmt_symbol_word_t kRmtPreamble  = {960, 1, 790, 0};
const rmt_symbol_word_t kRmtOne       = {220, 1, 980, 0};
const rmt_symbol_word_t kRmtZero      = {220, 1, 580, 0};
const rmt_symbol_word_t kRmtPostamble = {220, 1, 135, 0};

using namespace OpenShock;

namespace {
  struct ShockRollingState {
    int64_t lastFillTime  = 0;
    int64_t transmitStart = 0;
    uint8_t baseCounter   = 0;
  };

  std::unordered_map<uint16_t, ShockRollingState> s_shockState;

  // Shock intensity byte: [toggle:1][counter:3][level:4]
  // Toggle flips every ~1s, counter increments every ~2s (each toggle cycle).
  uint8_t shockRollingBits(uint16_t shockerId, int64_t now)
  {
    auto& state = s_shockState[shockerId];

    if (state.lastFillTime == 0 || (now - state.lastFillTime) > 200) {
      if (state.lastFillTime != 0) {
        state.baseCounter = (state.baseCounter + 1) & 0x7;
      }
      state.transmitStart = now;
    }
    state.lastFillTime = now;

    int64_t elapsed = now - state.transmitStart;
    int64_t periods = elapsed / 1000;

    bool toggle     = (periods % 2) != 0;
    uint8_t counter = (state.baseCounter + static_cast<uint8_t>(periods / 2)) & 0x7;

    return (static_cast<uint8_t>(toggle) << 7) | (counter << 4);
  }

  // Shock reads a 0-15 level from the low nibble; like D80, a non-zero intensity never rounds down to level 0.
  uint8_t shockLevel(uint8_t intensity)
  {
    return intensity > 0 ? std::max((intensity * 15) / 100, 1) : 0;
  }

  bool fillImpl(rmt_symbol_word_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint8_t rollingBits)
  {
    // Intensity must be between 0 and 100
    intensity = std::min(intensity, static_cast<uint8_t>(100));

    uint8_t typeVal = 0;
    switch (type) {
      case ShockerCommandType::Shock:
        typeVal   = 0b01100001;
        intensity = rollingBits | shockLevel(intensity);
        break;
      case ShockerCommandType::Vibrate:
        typeVal = 0b01110010;
        break;
      case ShockerCommandType::Sound:
        typeVal   = 0b10000100;
        intensity = 0;  // The remote always sends 0, I don't know what happens if you send something else.
        break;
      default:
        return false;  // Invalid type
    }

    uint8_t channelId = 0;  // CH1 is 0b0000 and CH2 is 0b1110 on my remote but other values probably work.

    // Payload layout: [channelId:4][typeU:4][transmitterId:16][intensity:8][typeL:4][channelId:4]
    uint64_t data = (static_cast<uint64_t>(channelId & 0xF) << 36) | (static_cast<uint64_t>(typeVal & 0xF0) << 28) | (static_cast<uint64_t>(shockerId) << 16) | (static_cast<uint64_t>(intensity) << 8) | (static_cast<uint64_t>(typeVal & 0xF) << 4)
                  | static_cast<uint64_t>(channelId & 0xF);

    // Shift the data left by 1 bit to append a zero
    data <<= 1;

    // Generate the sequence
    sequence[0] = kRmtPreamble;
    Rmt::Internal::EncodeBits<41>(sequence + 1, data, kRmtOne, kRmtZero);
    sequence[42] = kRmtPostamble;

    return true;
  }
}  // namespace

size_t Rmt::WellturnT330Alt1Encoder::GetBufferSize()
{
  return 43;
}

bool Rmt::WellturnT330Alt1Encoder::FillBuffer(rmt_symbol_word_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  return fillImpl(sequence, shockerId, type, intensity, 0);
}

bool Rmt::WellturnT330Alt1Encoder::RefillBuffer(rmt_symbol_word_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, int64_t nowMs)
{
  uint8_t rollingBits = type == ShockerCommandType::Shock ? shockRollingBits(shockerId, nowMs) : 0;
  return fillImpl(sequence, shockerId, type, intensity, rollingBits);
}
