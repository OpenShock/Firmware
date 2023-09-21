#include "Rmt/MainEncoder.h"

#include "Rmt/PetTrainerEncoder.h"
#include "Rmt/XlcEncoder.h"

#include <esp_log.h>

#include <unordered_map>

const char* const TAG = "RmtMainEncoder";

std::vector<rmt_data_t>
  ShockLink::Rmt::GetSequence(std::uint16_t shockerId, std::uint8_t method, std::uint8_t intensity, std::uint8_t shockerModel) {
  switch (shockerModel) {
    case 1:
      return ShockLink::Rmt::PetTrainerEncoder::GetSequence(shockerId, method, intensity);
    case 2:
      return ShockLink::Rmt::XlcEncoder::GetSequence(shockerId, 0, method, intensity);
    default:
      ESP_LOGE(TAG, "Unknown shocker model: %d", shockerModel);
      return {};
  }
}
std::shared_ptr<std::vector<rmt_data_t>> ShockLink::Rmt::GetZeroSequence(std::uint16_t shockerId, std::uint8_t shockerModel) {
  static std::unordered_map<std::uint16_t, std::shared_ptr<std::vector<rmt_data_t>>> _sequences;

  auto it = _sequences.find(shockerId);
  if (it != _sequences.end()) return it->second;

  std::shared_ptr<std::vector<rmt_data_t>> sequence;
  switch (shockerModel) {
    case 1:
      sequence = std::make_shared<std::vector<rmt_data_t>>(ShockLink::Rmt::PetTrainerEncoder::GetSequence(shockerId, 2, 0));
      break;
    case 2:
      sequence = std::make_shared<std::vector<rmt_data_t>>(ShockLink::Rmt::XlcEncoder::GetSequence(shockerId, 0, 2, 0));
      break;
    default:
      ESP_LOGE(TAG, "Unknown shocker model: %d", shockerModel);
      sequence = nullptr;
      break;
  }

  _sequences[shockerId] = sequence;

  return sequence;
}
