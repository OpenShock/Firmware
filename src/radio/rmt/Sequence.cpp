#include "radio/rmt/Sequence.h"

const char* const TAG = "Sequence";

#include "Logging.h"
#include "radio/rmt/CaiXianlinEncoder.h"
#include "radio/rmt/Petrainer998DREncoder.h"
#include "radio/rmt/PetrainerEncoder.h"
#include "radio/rmt/T330Encoder.h"
#include "radio/rmt/D80Encoder.h"

using namespace OpenShock;

inline static size_t getSequenceBufferSize(ShockerModelType shockerModelType)
{
  switch (shockerModelType) {
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::GetBufferSize();
    case ShockerModelType::Petrainer998DR:
      return Rmt::Petrainer998DREncoder::GetBufferSize();
    case ShockerModelType::Petrainer:
      return Rmt::PetrainerEncoder::GetBufferSize();
    case ShockerModelType::T330:
      return Rmt::T330Encoder::GetBufferSize();
    case ShockerModelType::D80:
      return Rmt::D80Encoder::GetBufferSize();
    default:
      return 0;
  }
}

inline static bool fillSequenceImpl(rmt_data_t* data, ShockerModelType modelType, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity)
{
  switch (modelType) {
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::FillBuffer(data, shockerId, 0, commandType, intensity);
    case ShockerModelType::Petrainer:
      return Rmt::PetrainerEncoder::FillBuffer(data, shockerId, commandType, intensity);
    case ShockerModelType::Petrainer998DR:
      return Rmt::Petrainer998DREncoder::FillBuffer(data, shockerId, commandType, intensity);
    case ShockerModelType::T330:
      return Rmt::T330Encoder::FillBuffer(data, shockerId, commandType, intensity);
    case ShockerModelType::D80:
      return Rmt::D80Encoder::FillBuffer(data, shockerId, commandType, intensity);
    default:
      OS_LOGE(TAG, "Unknown shocker model: %hhu", static_cast<uint8_t>(modelType));
      return false;
  }
}

Rmt::Sequence::Sequence(ShockerModelType shockerModel, uint16_t shockerId, int64_t transmitEnd)
  : m_data(nullptr)
  , m_size(getSequenceBufferSize(shockerModel))
  , m_transmitEnd(transmitEnd)
  , m_shockerId(shockerId)
  , m_shockerModel(shockerModel)
{
  if (m_size == 0) return;

  m_data = reinterpret_cast<rmt_data_t*>(malloc(m_size * 2 * sizeof(rmt_data_t)));
  if (m_data == nullptr) {
    m_size = 0;
    return;
  }

  if (!fillSequenceImpl(terminator(), m_shockerModel, m_shockerId, ShockerCommandType::Vibrate, 0)) {
    free(m_data);
    m_data = nullptr;
    m_size = 0;
    return;
  }
}

bool Rmt::Sequence::fill(ShockerCommandType commandType, uint8_t intensity)
{
  return fillSequenceImpl(payload(), m_shockerModel, m_shockerId, commandType, intensity);
}
