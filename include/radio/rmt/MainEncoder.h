#pragma once

#include "RmtSequence.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

namespace OpenShock::Rmt {
  RmtSequence GetSequence(ShockerModelType model, uint16_t shockerId, OpenShock::ShockerCommandType type, uint8_t intensity);
}  // namespace OpenShock::Rmt
