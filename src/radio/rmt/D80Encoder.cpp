#include "radio/rmt/D80Encoder.h"

#include "radio/rmt/internal/Shared.h"

#include "Checksum.h"

#include <algorithm>

const rmt_data_t kRmtPreamble  = {1900, 1, 4000, 0};
const rmt_data_t kRmtOne       = {900, 1, 300, 0};
const rmt_data_t kRmtZero      = {300, 1, 900, 0};
const rmt_data_t kRmtPostamble = {200, 1, 2200, 0};

using namespace OpenShock;

size_t Rmt::D80Encoder::GetBufferSize()
{
  return 42;
}

bool Rmt::D80Encoder::FillBuffer(rmt_data_t* sequence, uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  // Intensity must be between 0 and 15, this should mimic the rounding of the original remote which
  // allows you to select from 1-99 when the protocol only has 4 bits for intensity (0-15).
  if (intensity > 0)
    intensity = std::max((intensity*15)/100, 1);

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
      intensity = 0;  // The remote always sends 0, I don't know what happens if you send something else.
      break;
    default:
      return false;  // Invalid type
  }

  uint8_t channelId = 1;  // Channel ID is 1 or 2 for separate control or 3 for both channels

  // Payload layout: 00000100[shockerId:16][type:2][channelId:2][intensity:4]
  // The first byte is always 0x04, the two remotes both use this and it wont pair to other values.
  uint32_t payload = 0x04000000 | (static_cast<uint32_t>(shockerId) << 8) | (static_cast<uint32_t>(typeVal & 0x3) << 6) | (static_cast<uint32_t>(channelId & 0x3) << 4) | static_cast<uint32_t>(intensity & 0xF);

  // Calculate the checksum of the payload
  uint8_t checksum = Checksum::Sum8(payload);

  // Add the checksum to the payload
  uint64_t data = (static_cast<uint64_t>(payload) << 8) | static_cast<uint64_t>(checksum);

  // Generate the sequence
  sequence[0] = kRmtPreamble;
  Rmt::Internal::EncodeBits<40>(sequence + 1, data, kRmtOne, kRmtZero);
  sequence[41] = kRmtPostamble;

  return true;
}
