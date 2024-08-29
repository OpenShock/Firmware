#pragma once

#include "ShockerCommandType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <vector>

namespace OpenShock::Rmt::CaiXianlinEncoder {
  std::vector<rmt_data_t> GetSequence(uint16_t transmitterId, uint8_t channelId, OpenShock::ShockerCommandType type, uint8_t intensity);
}
