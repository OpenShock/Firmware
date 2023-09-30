#include "Rmt/PetTrainerEncoder.h"

const rmt_data_t kRmtPreamble  = {750, 1, 750, 0};
const rmt_data_t kRmtOne       = {200, 1, 1500, 0};
const rmt_data_t kRmtZero      = {200, 1, 750, 0};
const rmt_data_t kRmtPostamble = {200, 1, 7000, 0};

using namespace OpenShock;

std::vector<rmt_data_t>
  Rmt::PetTrainerEncoder::GetSequence(std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity) {
  std::uint8_t methodBit      = (0x80 | (1 << ((std::uint8_t)type - 1))) & 0xFF;
  std::uint8_t methodChecksum = 0xFF ^ ((1 << (8 - (std::uint8_t)type)) | 1);

  std::uint64_t data = (std::uint64_t(methodBit) << 32) | (std::uint64_t(shockerId) << 16) | (std::uint64_t(intensity) << 8)
                     | (std::uint64_t(methodChecksum) << 0);

  std::vector<rmt_data_t> pulses;
  pulses.reserve(42);

  pulses.push_back(kRmtPreamble);
  for (int bit_pos = 39; bit_pos >= 0; --bit_pos) {
    pulses.push_back((data >> bit_pos) & 1 ? kRmtOne : kRmtZero);
  }
  pulses.push_back(kRmtPostamble);

  return pulses;
}
