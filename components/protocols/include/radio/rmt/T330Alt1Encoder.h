#pragma once

#include "enums/ShockerCommandType.h"

#include <driver/rmt_types.h>

#include <cstdint>

namespace OpenShock::Rmt::WellturnT330Alt1Encoder {
  size_t GetBufferSize();
  bool FillBuffer(rmt_symbol_word_t* data, uint16_t shockerId, ShockerCommandType type, uint8_t intensity);
  // Re-encodes the frame with the shock rolling state (toggle + counter) for nowMs; call before every frame sent.
  bool RefillBuffer(rmt_symbol_word_t* data, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, int64_t nowMs);
}
