#pragma once

#include "enums/ShockerCommandType.h"

#include <driver/rmt_types.h>

#include <cstdint>

namespace OpenShock::Rmt::PetrainerEncoder {
  size_t GetBufferSize();
  bool FillBuffer(rmt_symbol_word_t* data, uint16_t shockerId, ShockerCommandType type, uint8_t intensity);
}
