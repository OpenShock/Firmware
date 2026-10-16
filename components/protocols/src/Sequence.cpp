#include "radio/rmt/Sequence.h"

const char* const TAG = "Sequence";

#include "Logging.h"
#include "radio/rmt/CaiXianlinEncoder.h"
#include "radio/rmt/D80Encoder.h"
#include "radio/rmt/Petrainer998DREncoder.h"
#include "radio/rmt/PetrainerEncoder.h"
#include "radio/rmt/T330Encoder.h"

using namespace OpenShock;

namespace {
  // One entry per supported model; adding a model means adding one line here.
  struct EncoderInfo {
    size_t (*bufferSize)();
    bool (*fill)(rmt_symbol_word_t* data, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity);
  };

  const EncoderInfo* findEncoder(ShockerModelType modelType)
  {
    static constexpr EncoderInfo kCaiXianlin {
      Rmt::CaiXianlinEncoder::GetBufferSize,
      [](rmt_symbol_word_t* data, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity) { return Rmt::CaiXianlinEncoder::FillBuffer(data, shockerId, 0, commandType, intensity); },
    };
    static constexpr EncoderInfo kPetrainer {Rmt::PetrainerEncoder::GetBufferSize, Rmt::PetrainerEncoder::FillBuffer};
    static constexpr EncoderInfo kPetrainer998DR {Rmt::Petrainer998DREncoder::GetBufferSize, Rmt::Petrainer998DREncoder::FillBuffer};
    static constexpr EncoderInfo kWellturnT330 {Rmt::WellturnT330Encoder::GetBufferSize, Rmt::WellturnT330Encoder::FillBuffer};
    static constexpr EncoderInfo kD80 {Rmt::D80Encoder::GetBufferSize, Rmt::D80Encoder::FillBuffer};

    switch (modelType) {
      case ShockerModelType::CaiXianlin:
        return &kCaiXianlin;
      case ShockerModelType::Petrainer:
        return &kPetrainer;
      case ShockerModelType::Petrainer998DR:
        return &kPetrainer998DR;
      case ShockerModelType::WellturnT330:
        return &kWellturnT330;
      case ShockerModelType::D80:
        return &kD80;
      default:
        OS_LOGE(TAG, "Unknown shocker model: %u", static_cast<unsigned>(modelType));
        return nullptr;
    }
  }
}  // namespace

Rmt::Sequence::Sequence(ShockerModelType shockerModel, uint16_t shockerId, int64_t transmitEnd)
  : m_data(nullptr)
  , m_size(0)
  , m_transmitEnd(transmitEnd)
  , m_shockerId(shockerId)
  , m_shockerModel(shockerModel)
  , m_commandType()
  , m_intensity(0)
  , m_terminatorSent(false)
{
  const EncoderInfo* encoder = findEncoder(shockerModel);
  if (encoder == nullptr) return;

  m_size = encoder->bufferSize();
  if (m_size == 0) return;

  m_data = static_cast<rmt_symbol_word_t*>(malloc(m_size * 2 * sizeof(rmt_symbol_word_t)));
  if (m_data == nullptr) {
    m_size = 0;
    return;
  }

  if (!encoder->fill(terminator(), m_shockerId, ShockerCommandType::Vibrate, 0)) {
    free(m_data);
    m_data = nullptr;
    m_size = 0;
    return;
  }
}

bool Rmt::Sequence::fill(ShockerCommandType commandType, uint8_t intensity)
{
  m_terminatorSent = false;  // A new payload needs its own terminator again
  m_commandType    = commandType;
  m_intensity      = intensity;

  const EncoderInfo* encoder = findEncoder(m_shockerModel);
  return encoder != nullptr && m_data != nullptr && encoder->fill(payload(), m_shockerId, commandType, intensity);
}

bool Rmt::Sequence::refill()
{
  if (m_data == nullptr) return false;

  switch (m_shockerModel) {
    case ShockerModelType::WellturnT330:
      return Rmt::WellturnT330Encoder::FillBuffer(payload(), m_shockerId, m_commandType, m_intensity);
    default:
      return true;
  }
}
