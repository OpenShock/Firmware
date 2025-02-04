#include "radio/rmt/Petrainer998DREncoder.h"

#include "radio/rmt/internal/Shared.h"

#include "Checksum.h"

#include <algorithm>

const rmt_data_t kRmtPreamble  = {1500, 1, 750, 0};
const rmt_data_t kRmtOne       = {750, 1, 250, 0};
const rmt_data_t kRmtZero      = {250, 1, 750, 0};
const rmt_data_t kRmtPostamble = {250, 1, 3750, 0};  // Some subvariants expect a quiet period between commands, this is a last 1 bit followed by a very long pause

using namespace OpenShock;

size_t Rmt::Petrainer998DREncoder::GetBufferSize()
{
  return 42;
}

bool Rmt::Petrainer998DREncoder::FillBuffer(rmt_data_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<uint8_t>(100));

  int typeShift = 0;
  switch (type) {
    case ShockerCommandType::Shock:
      typeShift = 0;
      break;
    case ShockerCommandType::Vibrate:
      typeShift = 1;
      break;
    case ShockerCommandType::Sound:
      typeShift = 2;
      break;
    case ShockerCommandType::Light:
      typeShift = 3;
      break;
    default:
      return false;  // Invalid type
  }

  uint8_t typeVal    = 1 << typeShift;
  uint8_t typeInvert = Checksum::ReverseInverseNibble(typeVal);

  // TODO: Channel argument?
  uint8_t channel       = 0b1000;  // Can be [1000] for CH1 or [1111] for CH2, 4 bits wide
  uint8_t channelInvert = Checksum::ReverseInverseNibble(channel);

  // Payload layout: [channel:4][typeVal:4][shockerID:16][intensity:8][typeInvert:4][channelInvert:4] (40 bits)
  uint64_t data
    = (static_cast<uint64_t>(channel & 0xF) << 36 | static_cast<uint64_t>(typeVal & 0xF) << 32 | static_cast<uint64_t>(shockerId & 0xFFFF) << 16 | static_cast<uint64_t>(intensity & 0xFF) << 8 | static_cast<uint64_t>(typeInvert & 0xF) << 4 | static_cast<uint64_t>(channelInvert & 0xF));

  // Generate the sequence
  sequence[0] = kRmtPreamble;
  Rmt::Internal::EncodeBits<40>(sequence + 1, data, kRmtOne, kRmtZero);
  sequence[41] = kRmtPostamble;

  return true;
}
