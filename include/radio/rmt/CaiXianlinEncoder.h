#pragma once

#include "RmtSequence.h"
#include "ShockerCommandType.h"

#include <cstdint>

namespace OpenShock::Rmt::CaiXianlinEncoder {
  RmtSequence GetSequence(uint16_t shockerId, uint8_t channelId, OpenShock::ShockerCommandType type, uint8_t intensity);
}
