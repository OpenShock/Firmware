#pragma once

#include <esp32-hal.h>

#include <cstdint>
#include <vector>

namespace ShockLink::Rmt::XlcEncoder {
  std::vector<rmt_data_t>
    GetSequence(std::uint16_t transmitterId, std::uint8_t channelId, std::uint8_t method, std::uint8_t intensity);
}
