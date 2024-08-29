#pragma once

#include <cstdint>

namespace OpenShock::Checksum {
  constexpr uint8_t CRC8(const uint8_t* data, std::size_t size) {
    uint8_t checksum = 0;
    for (std::size_t i = 0; i < size; ++i) {
      checksum += data[i];
    }
    return checksum;
  }
  template<typename T>
  constexpr uint8_t CRC8(T data) {
    return CRC8(reinterpret_cast<uint8_t*>(&data), sizeof(T));
  }
}  // namespace OpenShock::Checksum
