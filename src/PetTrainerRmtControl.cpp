#include "PetTrainerRmtControl.h"

#include <bitset>
#include <numeric>
#include <unordered_map>

const rmt_data_t startBit = {750, 1, 750, 0};
const rmt_data_t oneBit   = {200, 1, 1500, 0};
const rmt_data_t zeroBit  = {200, 1, 750, 0};
const rmt_data_t endBit   = {200, 1, 7000, 0};

const std::unordered_map<uint8_t, uint8_t> methodRemap = {
  {1, 1},
  {2, 2},
  {3, 4}
};

const std::unordered_map<uint8_t, uint8_t> methodChecksum = {
  {1, 0x7E},
  {2, 0xBE},
  {3, 0xDE}
};

std::vector<rmt_data_t> to_rmt_data(const std::vector<uint8_t>& data) {
  std::vector<rmt_data_t> pulses;

  pulses.push_back(startBit);

  for (auto byte : data) {
    std::bitset<8> bits(byte);
    for (int bit_pos = 7; bit_pos >= 0; --bit_pos) pulses.push_back(bits[bit_pos] ? oneBit : zeroBit);
  }

  pulses.push_back(endBit);

  return pulses;
}

std::vector<rmt_data_t>
  ShockLink::PetTrainerRmtControl::GetSequence(std::uint16_t shockerId, std::uint8_t method, std::uint8_t intensity) {
  std::vector<uint8_t> data
    = {(0x80 | methodRemap.at(method)), (shockerId >> 8) & 0xFF, shockerId & 0xFF, intensity, methodChecksum.at(method)};

  return to_rmt_data(data);
}
