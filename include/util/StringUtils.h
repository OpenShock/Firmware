#pragma once

#include <WString.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock {
  bool FormatToString(std::string& out, const char* format, ...);

  constexpr std::string_view StringTrimLeft(std::string_view view) {
    if (view.empty()) {
      return view;
    }

    std::size_t pos = 0;
    while (pos < view.size() && isspace(view[pos])) {
      ++pos;
    }

    return view.substr(pos);
  }
  constexpr std::string_view StringTrimRight(std::string_view view) {
    if (view.empty()) {
      return view;
    }

    std::size_t pos = view.size() - 1;
    while (pos > 0 && isspace(view[pos])) {
      --pos;
    }

    return view.substr(0, pos + 1);
  }
  constexpr std::string_view StringTrim(std::string_view view) {
    return StringTrimLeft(StringTrimRight(view));
  }
  constexpr bool StringStartsWith(std::string_view view, std::string_view prefix) {
    return view.size() >= prefix.size() && view.substr(0, prefix.size()) == prefix;
  }
  template<std::size_t N>
  constexpr bool TryStringSplit(std::string_view view, char delimiter, std::string_view (&out)[N]) {
    std::size_t pos = 0;
    std::size_t idx = 0;
    while (pos < view.size() && idx < N) {
      std::size_t nextPos = view.find(delimiter, pos);
      if (nextPos == std::string_view::npos) {
        nextPos = view.size();
      }

      out[idx] = view.substr(pos, nextPos - pos);
      pos      = nextPos + 1;
      ++idx;
    }

    return idx == N;
  }
  std::vector<std::string_view> StringSplit(std::string_view view, char delimiter, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  std::vector<std::string_view> StringSplit(std::string_view view, bool (*predicate)(char delimiter), std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  std::vector<std::string_view> StringSplitNewLines(std::string_view view, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  std::vector<std::string_view> StringSplitWhiteSpace(std::string_view view, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());

  bool StringIEquals(std::string_view a, std::string_view b);

  String StringToArduinoString(std::string_view view);
}  // namespace OpenShock
