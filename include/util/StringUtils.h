#pragma once

#include <WString.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenShock {
  bool FormatToString(std::string& out, const char* format, ...);

  constexpr std::string_view StringTrimLeft(std::string_view view)
  {
    if (view.empty()) {
      return view;
    }

    std::size_t pos = 0;
    while (pos < view.size() && isspace(view[pos]) != 0) {
      ++pos;
    }

    return view.substr(pos);
  }
  constexpr std::string_view StringTrimRight(std::string_view view)
  {
    if (view.empty()) {
      return view;
    }

    std::size_t pos = view.size() - 1;
    while (pos > 0 && isspace(view[pos]) != 0) {
      --pos;
    }

    return view.substr(0, pos + 1);
  }
  constexpr std::string_view StringTrim(std::string_view view)
  {
    return StringTrimLeft(StringTrimRight(view));
  }

  constexpr bool StringHasPrefix(std::string_view view, char prefix)
  {
    return !view.empty() && view.front() == prefix;
  }
  constexpr bool StringHasPrefix(std::string_view view, std::string_view prefix)
  {
    return view.size() >= prefix.size() && view.substr(0, prefix.size()) == prefix;
  }
  constexpr bool StringHasSuffix(std::string_view view, char suffix)
  {
    return !view.empty() && view.back() == suffix;
  }
  constexpr bool StringHasSuffix(std::string_view view, std::string_view suffix)
  {
    return view.size() >= suffix.size() && view.substr(view.size() - suffix.size(), view.size()) == suffix;
  }

  constexpr std::string_view StringRemovePrefix(std::string_view view, char prefix)
  {
    if (StringHasPrefix(view, prefix)) view.remove_prefix(1);
    return view;
  }
  constexpr std::string_view StringRemovePrefix(std::string_view view, std::string_view prefix)
  {
    if (StringHasPrefix(view, prefix)) view.remove_prefix(prefix.length());
    return view;
  }
  constexpr std::string_view StringRemoveSuffix(std::string_view view, char suffix)
  {
    if (StringHasSuffix(view, suffix)) view.remove_suffix(1);
    return view;
  }
  constexpr std::string_view StringRemoveSuffix(std::string_view view, std::string_view suffix)
  {
    if (StringHasSuffix(view, suffix)) view.remove_prefix(suffix.length());
    return view;
  }

  constexpr std::string_view StringBeforeFirst(std::string_view view, char delimiter)
  {
    size_t pos = view.find(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(0, pos);
  }
  constexpr std::string_view StringBeforeFirst(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.find(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(0, pos);
  }
  constexpr std::string_view StringBeforeLast(std::string_view view, char delimiter)
  {
    size_t pos = view.find_last_of(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(0, pos);
  }
  constexpr std::string_view StringBeforeLast(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.find_last_of(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(0, pos);
  }
  constexpr std::string_view StringAfterFirst(std::string_view view, char delimiter)
  {
    size_t pos = view.find(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(pos + 1);
  }
  constexpr std::string_view StringAfterFirst(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.find(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(pos + delimiter.length());
  }
  constexpr std::string_view StringAfterLast(std::string_view view, char delimiter)
  {
    size_t pos = view.find_last_of(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(pos + 1);
  }
  constexpr std::string_view StringAfterLast(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.find_last_of(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(pos + delimiter.length());
  }

  template<std::size_t N>
  constexpr bool TryStringSplit(std::string_view view, char delimiter, std::string_view (&out)[N])
  {
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
  constexpr std::pair<std::string_view, std::string_view> StringSplitByFirst(std::string_view view, char delimiter)
  {
    size_t pos = view.find(delimiter);
    return std::make_pair(view.substr(0, pos), pos == std::string_view::npos ? std::string_view() : view.substr(pos + 1));
  }
  constexpr std::pair<std::string_view, std::string_view> StringSplitByFirst(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.find(delimiter);
    return std::make_pair(view.substr(0, pos), pos == std::string_view::npos ? std::string_view() : view.substr(pos + delimiter.length()));
  }
  constexpr std::pair<std::string_view, std::string_view> StringSplitByLast(std::string_view view, char delimiter)
  {
    size_t pos = view.find_last_of(delimiter);
    return std::make_pair(view.substr(0, pos), pos == std::string_view::npos ? std::string_view() : view.substr(pos + 1));
  }
  constexpr std::pair<std::string_view, std::string_view> StringSplitByLast(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.find_last_of(delimiter);
    return std::make_pair(view.substr(0, pos), pos == std::string_view::npos ? std::string_view() : view.substr(pos + delimiter.length()));
  }

  bool StringIEquals(std::string_view a, std::string_view b);
  bool StringIContains(std::string_view haystack, std::string_view needle);
  bool StringHasPrefixIC(std::string_view view, std::string_view prefix);

  String StringToArduinoString(std::string_view view);
}  // namespace OpenShock
