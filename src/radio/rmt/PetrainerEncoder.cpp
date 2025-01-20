#include "radio/rmt/PetrainerEncoder.h"

#include "radio/rmt/internal/Shared.h"

const rmt_data_t kRmtPreamble  = {750, 1, 750, 0};
const rmt_data_t kRmtOne       = {200, 1, 1500, 0};
const rmt_data_t kRmtZero      = {200, 1, 750, 0};
const rmt_data_t kRmtPostamble = {200, 1, 7000, 0};

using namespace OpenShock;

static bool fillSequence(rmt_data_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<uint8_t>(100));

  uint8_t nShift = 0;
  switch (type) {
    case ShockerCommandType::Shock:
      nShift = 0;
      break;
    case ShockerCommandType::Vibrate:
      nShift = 1;
      break;
    case ShockerCommandType::Sound:
      nShift = 2;
      break;
    default:
      return false;  // Invalid type
  }

  // Type is 0x80 | (0x01 << nShift)
  uint8_t typeVal = (0x80 | (0x01 << nShift)) & 0xFF;

  // TypeSum is NOT(0x01 | (0x80 >> nShift))
  uint8_t typeSum = (~(0x01 | (0x80 >> nShift))) & 0xFF;

  // Payload layout: [methodBit:8][shockerId:16][intensity:8][methodChecksum:8]
  uint64_t data = (static_cast<uint64_t>(typeVal) << 32) | (static_cast<uint64_t>(shockerId) << 16) | (static_cast<uint64_t>(intensity) << 8) | static_cast<uint64_t>(typeSum);

  // Generate the sequence
  sequence[0] = kRmtPreamble;
  Rmt::Internal::EncodeBits<40>(sequence + 1, data, kRmtOne, kRmtZero);
  sequence[41] = kRmtPostamble;

  return true;
}

Rmt::RmtSequence Rmt::PetrainerEncoder::GetSequence(uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  Rmt::RmtSequence sequence(42);

  if (!fillSequence(sequence.payload(), shockerId, type, intensity)) return {};
  if (!fillSequence(sequence.terminator(), shockerId, ShockerCommandType::Vibrate, 0)) return {};

  return sequence;
}
