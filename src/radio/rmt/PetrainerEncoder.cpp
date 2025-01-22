#include "radio/rmt/PetrainerEncoder.h"

#include "radio/rmt/internal/Shared.h"

#include "Checksum.h"

#include <algorithm>

// Base
const rmt_data_t kV1RmtPreamble  = {750, 1, 750, 0};
const rmt_data_t kV1RmtOne       = {200, 1, 1500, 0};
const rmt_data_t kV1RmtZero      = {200, 1, 750, 0};
const rmt_data_t kV1RmtPostamble = {200, 1, 7000, 0};

// 998DR Type 1
const rmt_data_t kV2RmtPreamble  = {1500, 1, 750, 0};
const rmt_data_t kV2RmtOne       = {750, 1, 250, 0};
const rmt_data_t kV2RmtZero      = {250, 1, 750, 0};
const rmt_data_t kV2RmtPostamble = {250, 1, 3750, 0};  // Some subvariants expect a quiet period between commands, this is a last 1 bit followed by a very long pause

using namespace OpenShock;

size_t Rmt::PetrainerEncoder::GetBufferSize()
{
  return 42;
}

uint64_t Rmt::PetrainerEncoder::MakePayload(uint16_t shockerId, uint8_t channel, ShockerCommandType type, uint8_t intensity)
{
  uint8_t typeVal = 0;
  switch (type) {
    case ShockerCommandType::Shock:
      typeVal = 0b0001;
      break;
    case ShockerCommandType::Vibrate:
      typeVal = 0b0010;
      break;
    case ShockerCommandType::Sound:
      typeVal = 0b0100;
      break;
    case ShockerCommandType::Light:
      typeVal = 0b1000;
      break;
    default:
      return 0;  // Invalid command type
  }

  uint8_t channelVal = 0;
  switch (channel) {
    case 1:
      typeVal = 0b1000;
      break;
    case 2:
      typeVal = 0b1111;
      break;
    default:
      return 0;  // Invalid channel
  }

  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<uint8_t>(100));

  uint8_t channelInvert = Checksum::ReverseInverseNibble(channelVal);
  uint8_t typeInvert    = Checksum::ReverseInverseNibble(typeVal);

  // Payload layout: [channel:4][type:4][shockerID:16][intensity:8][typeInvert:4][channelInvert:4] (40 bits)
  return static_cast<uint64_t>(channelVal) << 36 | static_cast<uint64_t>(typeVal) << 32 | static_cast<uint64_t>(shockerId) << 16 | static_cast<uint64_t>(intensity) << 8 | static_cast<uint64_t>(typeInvert) << 4 | static_cast<uint64_t>(channelInvert);
}

void Rmt::PetrainerEncoder::EncodeType1Payload(rmt_data_t* sequence, uint64_t payload)
{
  // Generate the sequence
  sequence[0] = kV1RmtPreamble;
  Rmt::Internal::EncodeBits<40>(sequence + 1, payload, kV1RmtOne, kV1RmtZero);
  sequence[41] = kV1RmtPostamble;
}

void Rmt::PetrainerEncoder::EncodeType2Payload(rmt_data_t* sequence, uint64_t payload)
{
  // Generate the sequence
  sequence[0] = kV2RmtPreamble;
  Rmt::Internal::EncodeBits<40>(sequence + 1, payload, kV2RmtOne, kV2RmtZero);
  sequence[41] = kV2RmtPostamble;
}
