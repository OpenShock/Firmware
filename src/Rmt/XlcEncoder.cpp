#include "Rmt/XlcEncoder.h"

#include "Checksum.h"

const rmt_data_t kRmtPreamble = {1400, 1, 800, 0};
const rmt_data_t kRmtOne      = {800, 1, 300, 0};
const rmt_data_t kRmtZero     = {300, 1, 800, 0};

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::XlcEncoder::GetSequence(std::uint16_t transmitterId,
                                                     std::uint8_t channelId,
                                                     std::uint8_t method,
                                                     std::uint8_t intensity) {
  std::uint64_t data = (std::uint64_t(transmitterId) << 24) | (std::uint64_t(channelId & 0xF) << 20)
                     | (std::uint64_t(method & 0xF) << 16) | (std::uint64_t(intensity & 0xFF) << 8);

  data |= Checksum::CRC8(data) & 0xFF;
  using namespace OpenShock;

  data <<= 2;  // The 2 last bits are always 0. this is the postamble of the packet.

  std::vector<rmt_data_t> pulses;
  pulses.reserve(43);

  pulses.push_back(kRmtPreamble);
  for (int bit_pos = 39; bit_pos >= 0; --bit_pos) {
    pulses.push_back((data >> bit_pos) & 1 ? kRmtOne : kRmtZero);
  }

  return pulses;
}
