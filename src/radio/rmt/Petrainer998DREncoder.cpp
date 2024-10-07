#include "radio/rmt/Petrainer998DREncoder.h"

#include "radio/rmt/internal/Shared.h"

const rmt_data_t kRmtPreamble  = {1500, 1, 750, 0};
const rmt_data_t kRmtOne       = {750, 1, 250, 0};
const rmt_data_t kRmtZero      = {250, 1, 750, 0};
const rmt_data_t kRmtPostamble = {1500, 0, 1500, 0};  // Some subvariants expect a quiet period between commands

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::Petrainer998DREncoder::GetSequence(uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
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
    // case ShockerCommandType::Light:
    //   nShift = 3;
    //   break;
    default:
      return {};  // Invalid type
  }

  uint8_t typeVal    = 0b0001 << typeShift;
  uint8_t typeInvert = ~(0b1000 >> typeShift);

  // TODO: Channel argument?
  uint8_t channel       = 0b1000;  // Can be [1000] or [1111], 4 bits wide
  uint8_t channelInvert = 0b1110;  // Can be [1110] or [0000], 4 bits wide

  // TODO: Below ShockerID is 17 bits wide, and intensity is 7 bits wide. This is weird as ShockerID has 1 more bit and intensity has 1 less bit than 8 bit alignment. This needs investigation.
  // Payload layout: [channel:4][typeVal:4][shockerID:17][intensity:7][typeInvert:4][channelInvert:4] (40 bits)
  uint64_t data
    = (static_cast<uint64_t>(channel & 0b1111) << 36 | static_cast<uint64_t>(typeVal & 0b1111) << 32 | static_cast<uint64_t>(shockerId & 0x1FFFF) << 15 | static_cast<uint64_t>(intensity & 0x7F) << 8 | static_cast<uint64_t>(typeInvert & 0b1111) << 4 | static_cast<uint64_t>(channelInvert & 0b1111));

  std::vector<rmt_data_t> pulses;
  pulses.reserve(43);

  // Generate the sequence
  pulses.push_back(kRmtPreamble);
  Internal::EncodeBits<40>(pulses, data, kRmtOne, kRmtZero);
  pulses.push_back(kRmtZero);  // Idk why this is here, the decoded protocol has it
  pulses.push_back(kRmtPostamble);

  return pulses;
}
