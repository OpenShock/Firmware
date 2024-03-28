#include "radio/rmt/MainEncoder.h"

#include "Logging.h"
#include "radio/rmt/CaiXianlinEncoder.h"
#include "radio/rmt/PetrainerEncoder.h"
#include "radio/rmt/Petrainer998DREncoder.h"

#include <unordered_map>

const char* const TAG = "RmtMainEncoder";

using namespace OpenShock;

std::vector<rmt_data_t> Rmt::GetSequence(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity) {
  switch (model) {
    case ShockerModelType::Petrainer:
      return Rmt::PetrainerEncoder::GetSequence(shockerId, type, intensity);
    case ShockerModelType::Petrainer998DR:
      return Rmt::Petrainer998DREncoder::GetSequence(shockerId, type, intensity);
    case ShockerModelType::CaiXianlin:
      return Rmt::CaiXianlinEncoder::GetSequence(shockerId, 0, type, intensity);
    default:
      ESP_LOGE(TAG, "Unknown shocker model: %u", model);
      return {};
  }
}
/*
std::shared_ptr<std::vector<rmt_data_t>> Rmt::GetZeroSequence(ShockerModelType model, std::uint16_t shockerId) {
  static std::unordered_map<std::uint16_t, std::shared_ptr<std::vector<rmt_data_t>>> _sequences;

  auto it = _sequences.find(shockerId);
  if (it != _sequences.end()) return it->second; // FIXME: This is not thread-safe, and does not check if model is the same, causing a protocol mismatch

  std::shared_ptr<std::vector<rmt_data_t>> sequence;
  switch (model) {
    case ShockerModelType::Petrainer:
      sequence = std::make_shared<std::vector<rmt_data_t>>(Rmt::PetrainerEncoder::GetSequence(shockerId, ShockerCommandType::Vibrate, 0));
      break;
    case ShockerModelType::Petrainer998DR:
      sequence = std::make_shared<std::vector<rmt_data_t>>(Rmt::Petrainer998DREncoder::GetSequence(shockerId, ShockerCommandType::Vibrate, 0));
      break;
    case ShockerModelType::CaiXianlin:
      sequence = std::make_shared<std::vector<rmt_data_t>>(Rmt::CaiXianlinEncoder::GetSequence(shockerId, 0, ShockerCommandType::Vibrate, 0));
      break;
    default:
      ESP_LOGE(TAG, "Unknown shocker model: %u", model);
      sequence = nullptr;
      break;
  }

  _sequences[shockerId] = sequence;

  return sequence;
}
*/
