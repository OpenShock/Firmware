#include "radio/rmt/MainEncoder.h"

const char* const TAG = "RmtMainEncoder";

#include "Logging.h"
#include "radio/rmt/CaiXianlinEncoder.h"
#include "radio/rmt/Petrainer998DREncoder.h"
#include "radio/rmt/PetrainerEncoder.h"

using namespace OpenShock;

Rmt::RmtSequence Rmt::GetSequence(ShockerModelType model, uint16_t shockerId, ShockerCommandType type, uint8_t intensity)
{
  switch (model) {
    case ShockerModelType::Petrainer:
      return Rmt::PetrainerEncoder::GetSequence(shockerId, type, intensity);
    case ShockerModelType::Petrainer998DR:
      return Rmt::Petrainer998DREncoder::GetSequence(shockerId, type, intensity);
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::GetSequence(shockerId, 0, type, intensity);
    default:
      OS_LOGE(TAG, "Unknown shocker model: %u", model);
      return {};
  }
}
