#pragma once

#include <cstdint>

namespace OpenShock::ColorUtils {
  /// @brief RGB10 packed into 32 bits: red in bits 20-29, green in 10-19, blue in 0-9; bits 30-31 zero.
  inline constexpr uint32_t kRgb10Max = 0x3FF;

  /// @brief Packs three 10-bit channels (0-1023; higher bits are dropped) into RGB10.
  constexpr uint32_t PackRgb10(uint16_t red, uint16_t green, uint16_t blue)
  {
    return ((red & kRgb10Max) << 20) | ((green & kRgb10Max) << 10) | (blue & kRgb10Max);
  }

  constexpr uint16_t Rgb10Red(uint32_t packed) { return static_cast<uint16_t>((packed >> 20) & kRgb10Max); }
  constexpr uint16_t Rgb10Green(uint32_t packed) { return static_cast<uint16_t>((packed >> 10) & kRgb10Max); }
  constexpr uint16_t Rgb10Blue(uint32_t packed) { return static_cast<uint16_t>(packed & kRgb10Max); }

  /// @brief Widens a 10-bit channel to 16 bits by bit replication: the top bits repeated in the empty low ones. That
  ///        is v * 65535 / 1023 to within one 16-bit step, exact at both ends (0 -> 0, 1023 -> 65535).
  constexpr uint16_t Widen10To16(uint16_t value)
  {
    value &= kRgb10Max;
    return static_cast<uint16_t>((value << 6) | (value >> 4));
  }

  static_assert(Widen10To16(0) == 0);
  static_assert(Widen10To16(1023) == 65535);
  static_assert(PackRgb10(1023, 1023, 1023) == 0x3FFFFFFF);
  static_assert(Rgb10Red(PackRgb10(1, 2, 3)) == 1 && Rgb10Green(PackRgb10(1, 2, 3)) == 2 && Rgb10Blue(PackRgb10(1, 2, 3)) == 3);
}  // namespace OpenShock::ColorUtils
