#include "radio/rmt/MainEncoder.h"

const char* const TAG = "RmtMainEncoder";

#include "Logging.h"
#include "radio/rmt/CaiXianlinEncoder.h"
#include "radio/rmt/Petrainer998DREncoder.h"
#include "radio/rmt/PetrainerEncoder.h"
#include "radio/rmt/T330Encoder.h"

using namespace OpenShock;

static size_t getSequenceBufferSize(ShockerModelType shockerModelType)
{
  switch (shockerModelType) {
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::GetBufferSize();
    case ShockerModelType::Petrainer998DR:
      return Rmt::Petrainer998DREncoder::GetBufferSize();
    case ShockerModelType::Petrainer:
      return Rmt::PetrainerEncoder::GetBufferSize();
    default:
      return 0;
  }
}

Rmt::MainEncoder::MainEncoder(ShockerModelType shockerModel, uint16_t shockerId)
  : m_data(nullptr)
  , m_size(getSequenceBufferSize(shockerModel))
  , m_shockerModel(shockerModel)
  , m_shockerId(shockerId)
{
  if (m_size > 0) {
    m_data = reinterpret_cast<rmt_data_t*>(malloc(m_size * 2 * sizeof(rmt_data_t)));
  }
}

bool Rmt::MainEncoder::fillSequence(ShockerCommandType commandType, uint8_t intensity)
{
  switch (m_shockerModel) {
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::FillBuffer(m_data, m_shockerId, 0, commandType, intensity) && Rmt::CaiXianlinEncoder::FillBuffer(m_data + m_size, m_shockerId, 0, ShockerCommandType::Vibrate, 0);
    case ShockerModelType::Petrainer:
      return Rmt::PetrainerEncoder::FillBuffer(m_data, m_shockerId, commandType, intensity) && Rmt::PetrainerEncoder::FillBuffer(m_data + m_size, m_shockerId, ShockerCommandType::Vibrate, 0);
    case ShockerModelType::Petrainer998DR:
      return Rmt::Petrainer998DREncoder::FillBuffer(m_data, m_shockerId, commandType, intensity) && Rmt::Petrainer998DREncoder::FillBuffer(m_data + m_size, m_shockerId, ShockerCommandType::Vibrate, 0);
    default:
      OS_LOGE(TAG, "Unknown shocker model: %u", m_shockerModel);
      return false;
  }
}
