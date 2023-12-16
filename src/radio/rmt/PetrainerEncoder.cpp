#include "radio/rmt/PetrainerEncoder.h"

#include "radio/rmt/internal/Shared.h"

const rmt_data_t kRmtPreamble  = {750, 1, 750, 0};
const rmt_data_t kRmtOne       = {200, 1, 1500, 0};
const rmt_data_t kRmtZero      = {200, 1, 750, 0};
const rmt_data_t kRmtPostamble = {200, 1, 7000, 0};

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::PetrainerEncoder::GetSequence(std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity) {
  // Intensity must be between 0 and 100
  intensity = std::min(intensity, static_cast<std::uint8_t>(100));

  std::uint8_t nShift = 0;
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
    return {}; // Invalid type
  }

  // Type is 0x80 | (0x01 << nShift)
  std::uint8_t typeVal = (0x80 | (0x01 << nShift)) & 0xFF;

  // TypeSum is NOT(0x01 | (0x80 >> nShift))
  std::uint8_t typeSum = (~(0x01 | (0x80 >> nShift))) & 0xFF;

  // Payload layout: [methodBit:8][shockerId:16][intensity:8][methodChecksum:8]
  std::uint64_t data = (static_cast<std::uint64_t>(typeVal) << 32) | (static_cast<std::uint64_t>(shockerId) << 16) | (static_cast<std::uint64_t>(intensity) << 8) | static_cast<std::uint64_t>(typeSum);

  std::vector<rmt_data_t> pulses;
  pulses.reserve(42);

  // Generate the sequence
  pulses.push_back(kRmtPreamble);
  Internal::EncodeBits<40>(pulses, data, kRmtOne, kRmtZero);
  pulses.push_back(kRmtPostamble);

  return pulses;
}
