#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenShock {
  bool FormatToString(std::string& out, const char* format, ...) __attribute__((format(printf, 2, 3)));

  // ASCII-only, locale-independent and safe for bytes >= 0x80 (unlike isspace/tolower on a plain, signed char).
  constexpr bool CharIsSpace(char c)
  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
  }
  constexpr char CharToLower(char c)
  {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }

  constexpr std::string_view StringTrimLeft(std::string_view view)
  {
    std::size_t pos = 0;
    while (pos < view.size() && CharIsSpace(view[pos])) {
      ++pos;
    }

    return view.substr(pos);
  }
  constexpr std::string_view StringTrimRight(std::string_view view)
  {
    std::size_t len = view.size();
    while (len > 0 && CharIsSpace(view[len - 1])) {
      --len;
    }

    return view.substr(0, len);
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
    if (StringHasSuffix(view, suffix)) view.remove_suffix(suffix.length());
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
    size_t pos = view.rfind(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(0, pos);
  }
  constexpr std::string_view StringBeforeLast(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.rfind(delimiter);
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
    size_t pos = view.rfind(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(pos + 1);
  }
  constexpr std::string_view StringAfterLast(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.rfind(delimiter);
    if (pos == std::string_view::npos) return view;
    return view.substr(pos + delimiter.length());
  }

  // Splits `view` into N parts on `delimiter`. The first N-1 parts are the
  // delimited fields; the last part is the entire remainder (delimiters and
  // all), so nothing is silently dropped. Returns false only if there are too
  // few parts (fewer than N-1 delimiters). Callers that need each part to be a
  // single field validate the remainder themselves (e.g. Convert::ToUint8).
  template<std::size_t N>
  constexpr bool TryStringSplit(std::string_view view, char delimiter, std::string_view (&out)[N])
  {
    static_assert(N > 0, "TryStringSplit needs at least one output slot");

    std::size_t pos = 0;
    for (std::size_t idx = 0; idx < N - 1; ++idx) {
      std::size_t nextPos = view.find(delimiter, pos);
      if (nextPos == std::string_view::npos) {
        return false;  // too few parts
      }
      out[idx] = view.substr(pos, nextPos - pos);
      pos      = nextPos + 1;
    }

    out[N - 1] = view.substr(pos);  // remainder (may still contain delimiters)
    return true;
  }
  // Splits on every `delimiter`, keeping empty fields between delimiters ("a,,b" -> a, "", b). After `maxSplits`
  // splits the rest is returned as one final part. An empty input, or a trailing delimiter, adds no empty field.
  std::vector<std::string_view> StringSplit(std::string_view view, char delimiter, std::size_t maxSplits = std::numeric_limits<std::size_t>::max());
  // Splits on runs of characters matching `predicate`, dropping empty fields. After `maxSplits` fields the rest
  // (from the start of the next field) is returned as one final part.
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
    size_t pos = view.rfind(delimiter);
    return std::make_pair(view.substr(0, pos), pos == std::string_view::npos ? std::string_view() : view.substr(pos + 1));
  }
  constexpr std::pair<std::string_view, std::string_view> StringSplitByLast(std::string_view view, std::string_view delimiter)
  {
    size_t pos = view.rfind(delimiter);
    return std::make_pair(view.substr(0, pos), pos == std::string_view::npos ? std::string_view() : view.substr(pos + delimiter.length()));
  }

  // Case-insensitive ASCII comparisons. Length-aware, so they are safe on non-NUL-terminated views (e.g. a value
  // straight out of a JSON/jsmn parser).
  bool StringIEquals(std::string_view a, std::string_view b) noexcept;
  bool StringIContains(std::string_view haystack, std::string_view needle) noexcept;
  bool StringHasPrefixIC(std::string_view view, std::string_view prefix) noexcept;
}  // namespace OpenShock
