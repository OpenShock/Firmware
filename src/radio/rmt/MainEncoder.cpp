#include "radio/rmt/MainEncoder.h"

const char* const TAG = "RmtMainEncoder";

#include "Logging.h"
#include "radio/rmt/CaiXianlinEncoder.h"
#include "radio/rmt/PetrainerEncoder.h"
#include "radio/rmt/T330Encoder.h"

using namespace OpenShock;

static size_t getSequenceBufferSize(ShockerModelType shockerModelType)
{
  switch (shockerModelType) {
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::GetBufferSize();
    case ShockerModelType::Petrainer998DR:
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
    {
      uint64_t payload    = Rmt::CaiXianlinEncoder::MakePayload(m_shockerId, 0, commandType, intensity);
      uint64_t terminator = Rmt::CaiXianlinEncoder::MakePayload(m_shockerId, 0, ShockerCommandType::Vibrate, 0);
      if (payload == 0 || terminator == 0) return false;

      Rmt::CaiXianlinEncoder::EncodePayload(m_data, payload);
      Rmt::CaiXianlinEncoder::EncodePayload(m_data + m_size, terminator);

      return true;
    }
    case ShockerModelType::Petrainer:
    case ShockerModelType::Petrainer998DR:
    {
      uint64_t payload    = Rmt::PetrainerEncoder::MakePayload(m_shockerId, 1, commandType, intensity);
      uint64_t terminator = Rmt::PetrainerEncoder::MakePayload(m_shockerId, 1, ShockerCommandType::Vibrate, 0);
      if (payload == 0 || terminator == 0) return false;

      switch (m_shockerModel) {
        case ShockerModelType::Petrainer:
          Rmt::PetrainerEncoder::EncodeType1Payload(m_data, payload);
          Rmt::PetrainerEncoder::EncodeType1Payload(m_data + m_size, terminator);
          break;
        case ShockerModelType::Petrainer998DR:
          Rmt::PetrainerEncoder::EncodeType2Payload(m_data, payload);
          Rmt::PetrainerEncoder::EncodeType2Payload(m_data + m_size, terminator);
          break;
        default:
          return false;
      }

      return true;
    }
    default:
      OS_LOGE(TAG, "Unknown shocker model: %u", m_shockerModel);
      return false;
  }
}
