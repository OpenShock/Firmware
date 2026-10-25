#pragma once

#include <hal/gpio_types.h>

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace OpenShock::Convert {
  /**
   * @brief Parses a base-10 string into an integer of type T.
   *
   * Strict on purpose (serial args, config and backend values go through here): the whole string must be the number.
   * Rejected: empty input, leading zeros ("007"), negative zero ("-0"), a '+' sign, any whitespace, trailing
   * characters, a '-' on unsigned types, and values outside T's range (never truncated).
   *
   * @return The parsed value, or std::nullopt if the string is not a valid T.
   */
  template<std::integral T>
  [[nodiscard]] constexpr std::optional<T> ToInteger(std::string_view str)
  {
    static_assert(!std::is_same_v<std::remove_cv_t<T>, bool>, "Use ToBool for bool");

    // from_chars rejects empty input, '+', whitespace and a '-' on unsigned types itself, but accepts leading zeros.
    // A leading '0' is only valid as the whole string "0": rejects "007", "-0" and "-007".
    std::string_view digits = str.starts_with('-') ? str.substr(1) : str;
    if (digits.starts_with('0') && str.length() > 1) {
      return std::nullopt;
    }

    T value {};

    const char* const last = str.data() + str.length();

    auto [ptr, ec] = std::from_chars(str.data(), last, value);
    if (ec != std::errc {} || ptr != last) {
      return std::nullopt;  // Not a number, out of range, or trailing characters
    }

    return value;
  }

  [[nodiscard]] constexpr std::optional<int8_t> ToInt8(std::string_view str)
  {
    return ToInteger<int8_t>(str);
  }
  [[nodiscard]] constexpr std::optional<uint8_t> ToUint8(std::string_view str)
  {
    return ToInteger<uint8_t>(str);
  }
  [[nodiscard]] constexpr std::optional<int16_t> ToInt16(std::string_view str)
  {
    return ToInteger<int16_t>(str);
  }
  [[nodiscard]] constexpr std::optional<uint16_t> ToUint16(std::string_view str)
  {
    return ToInteger<uint16_t>(str);
  }
  [[nodiscard]] constexpr std::optional<int32_t> ToInt32(std::string_view str)
  {
    return ToInteger<int32_t>(str);
  }
  [[nodiscard]] constexpr std::optional<uint32_t> ToUint32(std::string_view str)
  {
    return ToInteger<uint32_t>(str);
  }
  [[nodiscard]] constexpr std::optional<int64_t> ToInt64(std::string_view str)
  {
    return ToInteger<int64_t>(str);
  }
  [[nodiscard]] constexpr std::optional<uint64_t> ToUint64(std::string_view str)
  {
    return ToInteger<uint64_t>(str);
  }
  [[nodiscard]] constexpr std::optional<std::size_t> ToSizeT(std::string_view str)
  {
    return ToInteger<std::size_t>(str);
  }

  /// @brief Case-insensitive "true" / "false".
  [[nodiscard]] std::optional<bool> ToBool(std::string_view str);

  /// @brief A GPIO number in [GPIO_NUM_NC, GPIO_NUM_MAX), with the same strictness as ToInteger.
  [[nodiscard]] std::optional<gpio_num_t> ToGpioNum(std::string_view str);
}  // namespace OpenShock::Convert
