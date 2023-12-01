#include "radio/rmt/CaiXianlinEncoder.h"

#include "Checksum.h"

// This is the encoder for the CaiXianlin shocker.
//
// It is based on the following documentation:
// https://wiki.openshock.org/hardware/shockers/caixianlin/#rf-specification

const rmt_data_t kRmtPreamble = {1400, 1, 800, 0};
const rmt_data_t kRmtOne      = {800, 1, 300, 0};
const rmt_data_t kRmtZero     = {300, 1, 800, 0};

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::CaiXianlinEncoder::GetSequence(std::uint16_t transmitterId, std::uint8_t channelId, ShockerCommandType type, std::uint8_t intensity) {
  // Intensity must be between 0 and 99
  intensity = std::min(intensity, (std::uint8_t)99);

  std::uint64_t data = (std::uint64_t(transmitterId) << 24) | (std::uint64_t(channelId & 0xF) << 20) | (std::uint64_t((std::uint8_t)type & 0xF) << 16) | (std::uint64_t(intensity & 0xFF) << 8);

  data |= Checksum::CRC8(data) & 0xFF;

  data <<= 2;  // The 2 last bits are always 0. this is the postamble of the packet.

  std::vector<rmt_data_t> pulses;
  pulses.reserve(43);

  pulses.push_back(kRmtPreamble);
  for (int bit_pos = 41; bit_pos >= 0; --bit_pos) {
    pulses.push_back((data >> bit_pos) & 1 ? kRmtOne : kRmtZero);
  }

  return pulses;
}
