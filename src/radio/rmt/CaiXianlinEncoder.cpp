#include "radio/rmt/CaiXianlinEncoder.h"

#include "radio/rmt/internal/Shared.h"

#include "Checksum.h"

#include <algorithm>

// This is the encoder for the CaiXianlin shocker.
//
// It is based on the following documentation:
// https://wiki.openshock.org/hardware/shockers/caixianlin/#rf-specification

const rmt_data_t kRmtPreamble = {1400, 1, 750, 0};
const rmt_data_t kRmtOne      = {750, 1, 250, 0};
const rmt_data_t kRmtZero     = {250, 1, 750, 0};

using namespace OpenShock;

size_t Rmt::CaiXianlinEncoder::GetBufferSize()
{
  return 44;
}

uint64_t Rmt::CaiXianlinEncoder::MakePayload(uint16_t shockerId, uint8_t channel, ShockerCommandType type, uint8_t intensity)
{
  uint8_t typeVal = 0;
  switch (type) {
    case ShockerCommandType::Shock:
      typeVal = 0x01;
      break;
    case ShockerCommandType::Vibrate:
      typeVal = 0x02;
      break;
    case ShockerCommandType::Sound:
      typeVal   = 0x03;
      intensity = 0;  // Sound intensity must be 0 for some shockers, otherwise it wont work, or they soft lock until restarted
      break;
    default:
      return 0;  // Invalid type
  }

  // Intensity must be between 0 and 99
  intensity = std::min(intensity, static_cast<uint8_t>(99));

  // Payload layout: [shockerId:16][channel:4][type:4][intensity:8]
  uint32_t payload = (static_cast<uint32_t>(shockerId) << 16) | (static_cast<uint32_t>(channel & 0xF) << 12) | (static_cast<uint32_t>(typeVal & 0xF) << 8) | static_cast<uint32_t>(intensity);

  // Calculate the checksum of the payload
  uint8_t checksum = Checksum::Sum8(payload);

  // Add the checksum to the payload
  uint64_t data = (static_cast<uint64_t>(payload) << 8) | static_cast<uint64_t>(checksum);

  // Shift the data left by 3 bits to add the postamble (3 bits of 0)
  return data << 3;
}

void Rmt::CaiXianlinEncoder::EncodePayload(rmt_data_t* sequence, uint64_t payload)
{
  sequence[0] = kRmtPreamble;
  Rmt::Internal::EncodeBits<43>(sequence + 1, payload, kRmtOne, kRmtZero);
}
