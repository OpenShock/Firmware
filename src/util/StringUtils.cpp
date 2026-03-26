#include "util/StringUtils.h"

const char* const TAG = "StringUtils";

#include "Logging.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>

bool OpenShock::FormatToString(std::string& out, const char* format, ...)
{
  const std::size_t STACK_BUFFER_SIZE = 128;

  char buffer[STACK_BUFFER_SIZE];
  char* bufferPtr = buffer;

  va_list args;

  // Try format with stack buffer.
  va_start(args, format);
  int result = vsnprintf(buffer, STACK_BUFFER_SIZE, format, args);
  va_end(args);

  // If result is negative, something went wrong.
  if (result < 0) {
    OS_LOGE(TAG, "Failed to format string");
    return false;
  }

  if (result >= STACK_BUFFER_SIZE) {
    // Account for null terminator.
    result += 1;

    // Allocate heap buffer.
    bufferPtr = new char[result];

    // Try format with heap buffer.
    va_start(args, format);
    result = vsnprintf(bufferPtr, result, format, args);
    va_end(args);

    // If we still fail, something is wrong.
    // Free heap buffer and return false.
    if (result < 0) {
      delete[] bufferPtr;
      OS_LOGE(TAG, "Failed to format string");
      return false;
    }
  }

  // Set output string.
  out = std::string(bufferPtr, result);

  // Free heap buffer if we used it.
  if (bufferPtr != buffer) {
    delete[] bufferPtr;
  }

  return true;
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
  return StringSplit(view, [](char c) { return isspace(c) != 0; }, maxSplits);
}

bool OpenShock::StringIEquals(std::string_view a, std::string_view b)
{
  if (a.size() != b.size()) return false;
  return strncasecmp(a.data(), b.data(), a.size()) == 0;
}
bool OpenShock::StringIContains(std::string_view haystack, std::string_view needle)
{
  if (haystack.size() < needle.size()) return false;
  if (haystack.size() == needle.size()) return StringIEquals(haystack, needle);

  return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char hc, char nc) { return tolower(hc) == tolower(nc); }) != haystack.end();
}
bool OpenShock::StringHasPrefixIC(std::string_view view, std::string_view prefix)
{
  if (view.size() < prefix.size()) return false;
  return StringIEquals(view.substr(0, prefix.size()), prefix);
}

String OpenShock::StringToArduinoString(std::string_view view)
{
  return String(view.data(), view.size());
}
