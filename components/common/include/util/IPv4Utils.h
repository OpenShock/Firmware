#pragma once

#include <cstddef>
#include <string_view>

namespace OpenShock::IPv4Utils {
  /// @brief Checks that a string is a dotted-decimal IPv4 address: four 0-255 octets.
  /// @remark Leading zeros are rejected, since lwIP reads them as octal. Usable in a static_assert.
  constexpr bool IsDottedDecimal(std::string_view str) noexcept
  {
    int octets = 0;
    while (true) {
      std::size_t digits = 0;
      int value          = 0;
      while (digits < str.size() && str[digits] >= '0' && str[digits] <= '9') {
        value = value * 10 + (str[digits] - '0');
        if (++digits > 3) return false;
      }
      if (digits == 0 || value > 255 || (digits > 1 && str[0] == '0')) return false;
      str.remove_prefix(digits);
      if (++octets == 4) return str.empty();
      if (str.empty() || str[0] != '.') return false;
      str.remove_prefix(1);
    }
  }

  /// @brief Checks that a string is a dotted-decimal address usable as a host on its /24 network: the last octet is
  /// 1-254, so it is neither the network nor the broadcast address.
  /// @remark Usable in a static_assert.
  constexpr bool IsSlash24Host(std::string_view str) noexcept
  {
    if (!IsDottedDecimal(str)) return false;

    int last = 0;
    for (char c : str.substr(str.rfind('.') + 1)) {
      last = last * 10 + (c - '0');
    }
    return last >= 1 && last <= 254;
  }
}  // namespace OpenShock::IPv4Utils
