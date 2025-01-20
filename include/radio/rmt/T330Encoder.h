#pragma once

#include "RmtSequence.h"
#include "ShockerCommandType.h"

#include <cstdint>

namespace OpenShock::Rmt::T330Encoder {
  RmtSequence GetSequence(uint16_t shockerId, OpenShock::ShockerCommandType type, uint8_t intensity);
}
