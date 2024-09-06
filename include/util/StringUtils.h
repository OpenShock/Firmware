#pragma once

#include <WString.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock {
  bool FormatToString(std::string& out, const char* format, ...);

  inline std::vector<std::string_view> StringSplit(const std::string_view view, char delimiter, std::size_t maxSplits = std::numeric_limits<std::size_t>::max()) {
    if (view.empty()) {
      return {};
    }

    std::vector<std::string_view> result = {};

    std::size_t pos    = 0;
    std::size_t splits = 0;
    while (pos < view.size() && splits < maxSplits) {
      std::size_t nextPos = view.find(delimiter, pos);
      if (nextPos == std::string_view::npos) {
        nextPos = view.size();
      }

      result.push_back(view.substr(pos, nextPos - pos));
      pos = nextPos + 1;
      ++splits;
    }

    if (pos < view.size()) {
      result.push_back(view.substr(pos));
    }

    return result;
  }
  inline std::vector<std::string_view> StringSplit(const std::string_view view, bool (*predicate)(char delimiter), std::size_t maxSplits = std::numeric_limits<std::size_t>::max()) {
    if (view.empty()) {
      return {};
    }

    std::vector<std::string_view> result = {};

    const char* start = nullptr;
    for (const char* ptr = view.begin(); ptr < view.end(); ++ptr) {
      if (predicate(*ptr)) {
        if (start != nullptr) {
          result.emplace_back(std::string_view(start, ptr - start));
          start = nullptr;
        }
      } else if (start == nullptr) {
        start = ptr;
      }
    }

    if (start != nullptr) {
      result.emplace_back(std::string_view(start, view.end() - start));
    }

    return result;
  }
  inline std::vector<std::string_view> StringSplitNewLines(const std::string_view view, std::size_t maxSplits = std::numeric_limits<std::size_t>::max()) {
    return StringSplit(
      view, [](char c) { return c == '\r' || c == '\n'; }, maxSplits
    );
  }
  inline std::vector<std::string_view> StringSplitWhiteSpace(const std::string_view view, std::size_t maxSplits = std::numeric_limits<std::size_t>::max()) {
    return StringSplit(
      view, [](char c) { return isspace(c) != 0; }, maxSplits
    );
  }
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
  inline String StringToArduinoString(std::string_view view) {
    return String(view.data(), view.size());
  }
}  // namespace OpenShock
