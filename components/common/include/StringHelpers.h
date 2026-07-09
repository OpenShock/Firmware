#pragma once

#include <cstddef>
#include <string_view>

namespace OpenShock {
  // Case-insensitive ASCII equality for two string views. Length-aware, so it is
  // safe on non-NUL-terminated views (e.g. a value straight out of a JSON/jsmn
  // parser). Header-only and constexpr so the foundation `common` component can
  // expose it without pulling in `util`.
  constexpr bool StringIEquals(std::string_view a, std::string_view b) noexcept
  {
    if (a.size() != b.size()) {
      return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
      char ca = a[i];
      char cb = b[i];
      if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
      if (ca != cb) {
        return false;
      }
    }

    return true;
  }
}  // namespace OpenShock
