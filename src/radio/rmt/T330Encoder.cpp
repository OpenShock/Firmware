#include "radio/rmt/T330Encoder.h"

#include "radio/rmt/internal/Shared.h"

#include <algorithm>

const rmt_data_t kRmtPreamble  = {960, 1, 790, 0};
const rmt_data_t kRmtOne       = {220, 1, 980, 0};
const rmt_data_t kRmtZero      = {220, 1, 580, 0};
const rmt_data_t kRmtPostamble = {220, 1, 135, 0};

using namespace OpenShock;

size_t Rmt::T330Encoder::GetBufferSize()
{
  return 43;
}

uint64_t Rmt::T330Encoder::MakePayload(uint16_t shockerId, uint8_t channel, ShockerCommandType type, uint8_t intensity)
{
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

  uint8_t channelVal = 0;  // CH1 is 0b0000 and CH2 is 0b1110 on my remote but other values probably work.
  switch (channel) {
    case 1:
      channelVal = 0b0000;
      break;
    case 2:
      channelVal = 0b1110;
      break;
    default:
      return false;  // Invalid channel
  }

  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<uint8_t>(100));

  // Payload layout: [channel:4][typeU:4][transmitterId:16][intensity:8][typeL:4][channel:4]
  uint64_t payload = (static_cast<uint64_t>(channelVal) << 36) | (static_cast<uint64_t>(typeVal & 0xF0) << 28) | (static_cast<uint64_t>(shockerId) << 16) | (static_cast<uint64_t>(intensity) << 8) | (static_cast<uint64_t>(typeVal & 0xF) << 4)
                   | static_cast<uint64_t>(channelVal);

  // Shift the data left by 1 bit to append a zero
  return payload << 1;
}

void Rmt::T330Encoder::EncodePayload(rmt_data_t* sequence, uint64_t payload)
{
  sequence[0] = kRmtPreamble;
  Rmt::Internal::EncodeBits<41>(sequence + 1, payload, kRmtOne, kRmtZero);
  sequence[42] = kRmtPostamble;
}
