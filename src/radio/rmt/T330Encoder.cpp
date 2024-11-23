#include "radio/rmt/T330Encoder.h"

#include "radio/rmt/internal/Shared.h"

const rmt_data_t kRmtPreamble  = {960, 1, 790, 0};
const rmt_data_t kRmtOne       = {220, 1, 980, 0};
const rmt_data_t kRmtZero      = {220, 1, 580, 0};
const rmt_data_t kRmtPostamble = {220, 1, 135, 0};

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::T330Encoder::GetSequence(uint16_t transmitterId, ShockerCommandType type, uint8_t intensity)
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
      return {};  // Invalid type
  }

  uint8_t channelId = 0; // CH1 is 0b0000 and CH2 is 0b1110 on my remote but other values probably work.

  // Payload layout: [channelId:4][typeU:4][transmitterId:16][intensity:8][typeL:4][channelId:4]
  uint64_t data = (static_cast<uint64_t>(channelId & 0xF) << 36) | (static_cast<uint64_t>(typeVal & 0xF0) << 28) | (static_cast<uint64_t>(transmitterId) << 16) | (static_cast<uint64_t>(intensity) << 8) | (static_cast<uint64_t>(typeVal & 0xF) << 4) | static_cast<uint64_t>(channelId & 0xF);

  // Shift the data left by 1 bit to append a zero
  data <<= 1;

  std::vector<rmt_data_t> pulses;
  pulses.reserve(43);

  // Generate the sequence
  pulses.push_back(kRmtPreamble);
  Internal::EncodeBits<41>(pulses, data, kRmtOne, kRmtZero);
  pulses.push_back(kRmtPostamble);

  return pulses;
}
