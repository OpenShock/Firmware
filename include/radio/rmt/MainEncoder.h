#pragma once

#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace OpenShock::Rmt {
  std::vector<rmt_data_t> GetSequence(ShockerModelType model, std::uint16_t shockerId, OpenShock::ShockerCommandType type, std::uint8_t intensity);
  inline std::vector<rmt_data_t> GetZeroSequence(ShockerModelType model, std::uint16_t shockerId) {
    return GetSequence(model, shockerId, ShockerCommandType::Vibrate, 0);
  }
}
