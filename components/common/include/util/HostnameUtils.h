#pragma once

#include <string_view>

namespace OpenShock::HostnameUtils {
  /// @brief Checks that a string is a usable hostname: an RFC 1123 label of 1-32 letters, digits and hyphens
  /// (esp_netif's limit), not starting or ending with a hyphen.
  /// @remark Usable in a static_assert.
  constexpr bool IsValid(std::string_view hostname) noexcept
  {
    if (hostname.empty() || hostname.size() > 32 || hostname.front() == '-' || hostname.back() == '-') {
      return false;
    }

    for (char c : hostname) {
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
      if (!ok) {
        return false;
      }
    }

    return true;
  }
}  // namespace OpenShock::HostnameUtils
