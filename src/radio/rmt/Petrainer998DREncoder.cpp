#include "radio/rmt/Petrainer998DREncoder.h"

#include "radio/rmt/internal/Shared.h"

const rmt_data_t kRmtPreamble  = {1500, 1, 750, 0};
const rmt_data_t kRmtOne       = {750, 1, 250, 0};
const rmt_data_t kRmtZero      = {250, 1, 750, 0};

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::Petrainer998DREncoder::GetSequence(std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity) {
  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<std::uint8_t>(100));

  std::uint8_t typeVal = 0;
  // typeInvert has the value of typeVal but bits are reversed and inverted
  std::uint8_t typeInvert = 0;
  switch (type) {
  case ShockerCommandType::Shock:
    typeVal    = 0b0001;
    typeInvert = 0b0111;
    break;
  case ShockerCommandType::Vibrate:
    typeVal    = 0b0010;
    typeInvert = 0b1011;
    break;
  case ShockerCommandType::Sound:
    typeVal    = 0b0100;
    typeInvert = 0b1101;
    break;
  // case ShockerCommandType::Light:
  //   typeVal    = 0b1000;
  //   typeInvert = 0b1110;
  //   break;
  default:
    return {}; // Invalid type
  }

  // TODO: Channel argument?
  // Can be [000] or [111], 3 bits wide
  std::uint8_t channel = 0b000;
  std::uint8_t channelInvert = 0b111;

  // Payload layout: [channel:3][typeVal:4][shockerID:17][intensity:7][typeInvert:4][channelInvert:3]
  std::uint64_t data = (static_cast<std::uint64_t>(channel & 0b111) << 35 | static_cast<std::uint64_t>(typeVal & 0b1111) << 31 | static_cast<std::uint64_t>(shockerId & 0x1FFFF) << 14 | static_cast<std::uint64_t>(intensity & 0x7F) << 7 | static_cast<std::uint64_t>(typeInvert & 0b1111) << 3 | static_cast<std::uint64_t>(channelInvert & 0b111));

  std::vector<rmt_data_t> pulses;
  pulses.reserve(42);

  // Generate the sequence
  pulses.push_back(kRmtPreamble);
  pulses.push_back(kRmtOne);
  Internal::EncodeBits<38>(pulses, data, kRmtOne, kRmtZero);
  pulses.push_back(kRmtZero);
  pulses.push_back(kRmtZero);

  return pulses;
}
