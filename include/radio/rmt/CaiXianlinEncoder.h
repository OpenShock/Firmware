#pragma once

#include "ShockerCommandType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>

namespace OpenShock::Rmt::CaiXianlinEncoder {
  size_t GetBufferSize();
  bool FillBuffer(rmt_data_t* data, uint16_t shockerId, uint8_t channelId, ShockerCommandType type, uint8_t intensity);
}
