#include <vector>
#include "esp32-hal.h"

namespace PetTrainerRmtControl
{
    std::vector<rmt_data_t> GetSequence(uint16_t shockerId, uint8_t method, uint8_t intensity);
}