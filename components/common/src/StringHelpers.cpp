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

  return std::ranges::search(haystack, needle, lowercaseEqual).begin() != haystack.end();
}
bool OpenShock::StringHasPrefixIC(std::string_view view, std::string_view prefix) noexcept
{
  if (view.size() < prefix.size()) return false;
  return std::ranges::equal(view.substr(0, prefix.size()), prefix, lowercaseEqual);
}
