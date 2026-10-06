#include "StringHelpers.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>

bool OpenShock::FormatToString(std::string& out, const char* format, ...)
{
  va_list args;

  // Measure, then format straight into the string (C++11 guarantees the trailing NUL slot).
  va_start(args, format);
  int length = vsnprintf(nullptr, 0, format, args);
  va_end(args);

  if (length < 0) {
    return false;
  }

  out.resize(static_cast<std::size_t>(length));

  va_start(args, format);
  int written = vsnprintf(out.data(), out.size() + 1, format, args);
  va_end(args);

  return written == length;
}

std::vector<std::string_view> OpenShock::StringSplit(std::string_view view, char delimiter, std::size_t maxSplits)
{
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

std::vector<std::string_view> OpenShock::StringSplit(std::string_view view, bool (*predicate)(char delimiter), std::size_t maxSplits)
{
  if (view.empty()) {
    return {};
  }

  std::vector<std::string_view> result = {};

  const char* start = nullptr;
  for (const char* ptr = view.begin(); ptr < view.end(); ++ptr) {
    if (predicate(*ptr)) {
      if (start != nullptr) {
        result.emplace_back(start, ptr - start);
        start = nullptr;
      }
    } else if (start == nullptr) {
      start = ptr;
      if (result.size() >= maxSplits) {
        break;  // Remainder becomes the final part below
      }
    }
  }

  if (start != nullptr) {
    result.emplace_back(start, view.end() - start);
  }

  return result;
}

std::vector<std::string_view> OpenShock::StringSplitNewLines(std::string_view view, std::size_t maxSplits)
{
  return StringSplit(view, [](char c) { return c == '\r' || c == '\n'; }, maxSplits);
}

std::vector<std::string_view> OpenShock::StringSplitWhiteSpace(std::string_view view, std::size_t maxSplits)
{
  return StringSplit(view, [](char c) { return CharIsSpace(c); }, maxSplits);
}

static bool lowercaseEqual(char a, char b)
{
  return OpenShock::CharToLower(a) == OpenShock::CharToLower(b);
}

bool OpenShock::StringIEquals(std::string_view a, std::string_view b) noexcept
{
  return std::ranges::equal(a, b, lowercaseEqual);
}
bool OpenShock::StringIContains(std::string_view haystack, std::string_view needle) noexcept
{
  if (haystack.size() < needle.size()) return false;
  if (haystack.size() == needle.size()) return std::ranges::equal(haystack, needle, lowercaseEqual);

  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), lowercaseEqual) != haystack.end();
}
bool OpenShock::StringHasPrefixIC(std::string_view view, std::string_view prefix) noexcept
{
  if (view.size() < prefix.size()) return false;
  return std::ranges::equal(view.substr(0, prefix.size()), prefix, lowercaseEqual);
}
