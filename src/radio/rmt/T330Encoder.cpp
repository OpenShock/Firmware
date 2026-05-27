#include "radio/rmt/T330Encoder.h"

#include "radio/rmt/internal/Shared.h"

#include <algorithm>
#include <unordered_map>

const rmt_data_t kRmtPreamble  = {960, 1, 790, 0};
const rmt_data_t kRmtOne       = {220, 1, 980, 0};
const rmt_data_t kRmtZero      = {220, 1, 580, 0};
const rmt_data_t kRmtPostamble = {220, 1, 135, 0};

using namespace OpenShock;

struct ShockRollingState {
  uint8_t counter = 0;
  bool toggle     = false;
};

static std::unordered_map<uint16_t, ShockRollingState> s_shockState;

size_t Rmt::WellturnT330Encoder::GetBufferSize()
{
  return 43;
}

bool Rmt::WellturnT330Encoder::FillBuffer(rmt_data_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<uint8_t>(100));

  uint8_t typeVal = 0;
  switch (type) {
    case ShockerCommandType::Shock:
      typeVal = 0b01100001;
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

  // Shock intensity byte: [toggle:1][counter:3][level:4]
  // The collar reads shock intensity from the lower nibble (0-15), not as a raw 0-100 value.
  uint8_t intensityByte = intensity;
  if (type == ShockerCommandType::Shock) {
    auto& state   = s_shockState[shockerId];
    uint8_t level = (intensity * 15) / 100;
    intensityByte = (static_cast<uint8_t>(state.toggle) << 7) | ((state.counter & 0x7) << 4) | (level & 0xF);
    state.toggle  = !state.toggle;
    if (!state.toggle) {
      state.counter = (state.counter + 1) & 0x7;
    }
  }

  uint8_t channelId = 0;  // CH1 is 0b0000 and CH2 is 0b1110 on my remote but other values probably work.

  // Payload layout: [channelId:4][typeU:4][transmitterId:16][intensity:8][typeL:4][channelId:4]
  uint64_t data = (static_cast<uint64_t>(channelId & 0xF) << 36) | (static_cast<uint64_t>(typeVal & 0xF0) << 28) | (static_cast<uint64_t>(shockerId) << 16) | (static_cast<uint64_t>(intensityByte) << 8) | (static_cast<uint64_t>(typeVal & 0xF) << 4)
                | static_cast<uint64_t>(channelId & 0xF);

  // Shift the data left by 1 bit to append a zero
  data <<= 1;

  // Generate the sequence
  sequence[0] = kRmtPreamble;
  Rmt::Internal::EncodeBits<41>(sequence + 1, data, kRmtOne, kRmtZero);
  sequence[42] = kRmtPostamble;

  return true;
}
