#include <esp32-hal.h>
#include <vector>
#include <cstdint>

namespace ShockLink::XlcRmtControl
{
    std::vector<rmt_data_t> GetSequence(std::uint16_t shockerId, std::uint8_t method, std::uint8_t intensity);
}