#pragma once

#include "ShockerCommandType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <vector>

namespace OpenShock::Rmt::T330Encoder {
  std::vector<rmt_data_t> GetSequence(uint16_t transmitterId, OpenShock::ShockerCommandType type, uint8_t intensity);
}
