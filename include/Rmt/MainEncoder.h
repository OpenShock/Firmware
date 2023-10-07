#pragma once

#include "ShockerCommandType.h"

#include <esp32-hal.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace OpenShock::Rmt {
  std::vector<rmt_data_t>
    GetSequence(std::uint16_t shockerId, OpenShock::ShockerCommandType type, std::uint8_t intensity, std::uint8_t shockerModel);
  std::shared_ptr<std::vector<rmt_data_t>> GetZeroSequence(std::uint16_t shockerId, std::uint8_t shockerModel);
}
