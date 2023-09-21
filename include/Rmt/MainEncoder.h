#pragma once

#include <esp32-hal.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace ShockLink::Rmt {
  std::vector<rmt_data_t>
    GetSequence(std::uint16_t shockerId, std::uint8_t method, std::uint8_t intensity, std::uint8_t shockerModel);
  std::shared_ptr<std::vector<rmt_data_t>> GetZeroSequence(std::uint16_t shockerId, std::uint8_t shockerModel);
}
