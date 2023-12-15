#pragma once

#include "ShockerCommandType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <vector>

namespace OpenShock::Rmt::PetrainerEncoder {
  std::vector<rmt_data_t> GetSequence(std::uint16_t shockerId, OpenShock::ShockerCommandType type, std::uint8_t intensity);
}
