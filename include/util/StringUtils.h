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
  std::vector<std::string_view> StringSplit(const std::string_view view, char delimiter, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  std::vector<std::string_view> StringSplit(const std::string_view view, bool (*predicate)(char delimiter), std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  std::vector<std::string_view> StringSplitNewLines(const std::string_view view, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  std::vector<std::string_view> StringSplitWhiteSpace(const std::string_view view, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  String StringToArduinoString(std::string_view view);
}  // namespace OpenShock
