#pragma once

#include <cstdint>
#include <memory>

#if __cplusplus >= 202'002L
// #error Take into use C++20 std::integral instead of this macro logic
#endif

#define SUM8_INT_FN(Type)                                       \
  constexpr uint8_t Sum8(Type data)                             \
  {                                                             \
    uint8_t result = 0;                                         \
    for (int i = 0; i < sizeof(Type); ++i) {                    \
      result += static_cast<uint8_t>((data >> (i * 8)) & 0xFF); \
    }                                                           \
    return result;                                              \
  }

namespace OpenShock::Checksum {
  constexpr uint8_t Sum8(const uint8_t* data, std::size_t size)
  {
    uint8_t checksum = 0;
    for (std::size_t i = 0; i < size; ++i) {
      checksum += data[i];
    }
    return checksum;
  }
  template<typename T>
  constexpr uint8_t Sum8(const T& data)
  {
    return Sum8(reinterpret_cast<const uint8_t*>(std::addressof(data)), sizeof(T));
  }

  SUM8_INT_FN(int16_t)
  SUM8_INT_FN(uint16_t)
  SUM8_INT_FN(int32_t)
  SUM8_INT_FN(uint32_t)
  SUM8_INT_FN(int64_t)
  SUM8_INT_FN(uint64_t)

  
  /**
   * Make sure the uint8 only has its high bits (0x0F) set before using this function
   */
  constexpr uint8_t ReverseNibble(uint8_t b) {
    return (0xF7B3D591E6A2C480 >> (b * 4)) & 0xF; // Trust me bro
  }
  
  /**
   * Make sure the uint8 only has its high bits (0x0F) set before using this function
   */
  constexpr uint8_t ReverseInverseNibble(uint8_t b) {
    return (0x084C2A6E195D3B7F >> (b * 4)) & 0xF; // Trust me bro
  }
}  // namespace OpenShock::Checksum

#undef SUM8_INT_FN
