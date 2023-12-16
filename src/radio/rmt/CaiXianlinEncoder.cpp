#include "radio/rmt/CaiXianlinEncoder.h"

#include "radio/rmt/internal/Shared.h"

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
  intensity = std::min(intensity, static_cast<std::uint8_t>(99));

  std::uint8_t typeVal = 0;
  switch (type) {
  case ShockerCommandType::Shock:
    typeVal = 0x01;
    break;
  case ShockerCommandType::Vibrate:
    typeVal = 0x02;
    break;
  case ShockerCommandType::Sound:
    typeVal = 0x03;
    break;
  default:
    return {}; // Invalid type
  }

  // Payload layout: [transmitterId:16][channelId:4][type:4][intensity:8]
  std::uint32_t payload = (static_cast<std::uint32_t>(transmitterId & 0xFFFF) << 16) | (static_cast<std::uint32_t>(channelId & 0xF) << 12) | (static_cast<std::uint32_t>(typeVal) << 8) | static_cast<std::uint32_t>(intensity & 0xFF);

  // Calculate the checksum of the payload
  std::uint8_t checksum = Checksum::CRC8(payload);

  // Add the checksum to the payload
  std::uint64_t data = (static_cast<std::uint64_t>(payload) << 8) | static_cast<std::uint64_t>(checksum);

  // Shift the data left by 3 bits to add the postamble (3 bits of 0)
  data <<= 3;

  std::vector<rmt_data_t> pulses;
  pulses.reserve(44);

  // Generate the sequence
  pulses.push_back(kRmtPreamble);
  Internal::EncodeBits<43>(pulses, data, kRmtOne, kRmtZero);

  return pulses;
}
