#include "Logging.h"

#include "serial/Serial.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstring>

static std::atomic<uint8_t> s_runtimeLevel = OpenShock::Log::kCompiledLevel;

uint8_t OpenShock::Log::GetRuntimeLevel()
{
  return s_runtimeLevel.load(std::memory_order_relaxed);
}

uint8_t OpenShock::Log::SetRuntimeLevel(uint8_t level)
{
  level = std::min(level, kCompiledLevel);
  s_runtimeLevel.store(level, std::memory_order_relaxed);
  return level;
}

// The level of an OS_LOG* line, from the letter after OPENSHOCK_LOG_FORMAT_PREFIX. Anything else (no prefix, an
// unknown letter, or OS_LOGN) reads as NONE, which is always printed.
static uint8_t levelOf(const char* fmt)
{
  constexpr std::size_t kPrefixLen = sizeof(OPENSHOCK_LOG_FORMAT_PREFIX) - 1;
  if (std::strncmp(fmt, OPENSHOCK_LOG_FORMAT_PREFIX, kPrefixLen) != 0) {
    return OPENSHOCK_LOG_LEVEL_NONE;
  }

  switch (fmt[kPrefixLen]) {
    case 'E':
      return OPENSHOCK_LOG_LEVEL_ERROR;
    case 'W':
      return OPENSHOCK_LOG_LEVEL_WARN;
    case 'I':
      return OPENSHOCK_LOG_LEVEL_INFO;
    case 'D':
      return OPENSHOCK_LOG_LEVEL_DEBUG;
    case 'V':
      return OPENSHOCK_LOG_LEVEL_VERBOSE;
    default:
      return OPENSHOCK_LOG_LEVEL_NONE;
  }
}

// Forwards OpenShock's OS_LOG* output to the low-level serial transport, which formats the line and writes the raw
// bytes to the console port. This bypasses C stdio (no vprintf/stdout); before the serial driver is installed,
// Serial::Write falls back to the ROM serial output so early-boot logs survive.
extern "C" int openshock_log_printf(const char* fmt, ...)
{
  if (levelOf(fmt) > s_runtimeLevel.load(std::memory_order_relaxed)) {
    return 0;
  }

  va_list args;
  va_start(args, fmt);
  int len = OpenShock::Serial::VWritef(fmt, args);
  va_end(args);

  return len;
}
