#include "LRmtControl.h"
#include <vector>
#include "esp32-hal.h"
#include <bitset>
#include <numeric>

namespace LRmtControl
{
  const rmt_data_t startBit = {
      1400,
      1,
      800,
      0};

  const rmt_data_t oneBit = {
      800,
      1,
      300,
      0};

  const rmt_data_t zeroBit = {
      300,
      1,
      800,
      0};

  std::vector<rmt_data_t> to_rmt_data(const std::vector<uint8_t> &data)
  {
    std::vector<rmt_data_t> pulses;

    pulses.push_back(startBit);

    for (auto byte : data)
    {
      std::bitset<8> bits(byte);
      for (int bit_pos = 7; bit_pos >= 0; --bit_pos)
        pulses.push_back(bits[bit_pos] ? oneBit : zeroBit);
    }

    for (int i = 0; i < 3; ++i)
      pulses.push_back(zeroBit);

    return pulses;
  }

  std::vector<rmt_data_t> GetSequence(uint16_t shockerId, uint8_t method, uint8_t intensity)
  {
    std::vector<uint8_t> data = {
        (shockerId >> 8) & 0xFF,
        shockerId & 0xFF,
        method,
        intensity};

    int checksum = std::accumulate(data.begin(), data.end(), 0) & 0xff;
    data.push_back(checksum);

    return to_rmt_data(data);
  }
}