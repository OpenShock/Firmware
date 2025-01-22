#pragma once

#include "ShockerCommandType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>

namespace OpenShock::Rmt::PetrainerEncoder {
  size_t GetBufferSize();
  uint64_t MakePayload(uint16_t shockerId, uint8_t channel, ShockerCommandType type, uint8_t intensity);
  void EncodeType1Payload(rmt_data_t* data, uint64_t payload);
  void EncodeType2Payload(rmt_data_t* data, uint64_t payload);
}  // namespace OpenShock::Rmt::PetrainerEncoder
