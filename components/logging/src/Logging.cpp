#include "Logging.h"

#include "LogTap.h"
#include "serial/Serial.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::atomic<OpenShock::Logging::LogTap> s_tap {nullptr};

void OpenShock::Logging::SetTap(LogTap tap)
{
  s_tap.store(tap, std::memory_order_release);
}

// Formats the console line once - prefix, message, "\r\n" - and writes the raw bytes to the low-level serial transport
// in one call, then hands the message part of the same buffer to the tap. This bypasses C stdio (no vprintf/stdout);
// before the serial driver is installed, Serial::Write falls back to the ROM serial output so early-boot logs survive.
// A line too long for the stack buffer goes through the heap, and is truncated rather than dropped if that allocation
// fails.
extern "C" void openshock_log_write(char level, const char* tag, const char* file, int line, const char* func, const char* fmt, ...)
{
  static constexpr char kLineEnd[]         = "\r\n";
  static constexpr std::size_t kLineEndLen = sizeof(kLineEnd) - 1;

  char stackBuf[320];

  int prefixLen = snprintf(stackBuf, sizeof(stackBuf), "[%" PRId64 "][%c][%s:%d] %s(): [%s] ", OpenShock::millis(), level, file, line, func, tag);
  if (prefixLen < 0) {
    return;
  }
  // A prefix that fills the buffer is cut, leaving room for at least the line ending
  std::size_t prefix = std::min(static_cast<std::size_t>(prefixLen), sizeof(stackBuf) - kLineEndLen - 1);

  va_list args;
  va_start(args, fmt);
  va_list retryArgs;
  va_copy(retryArgs, args);

  int messageLen = vsnprintf(stackBuf + prefix, sizeof(stackBuf) - prefix, fmt, args);
  va_end(args);
  if (messageLen < 0) {
    va_end(retryArgs);
    return;
  }

  char* buffer        = stackBuf;
  char* heapBuf       = nullptr;
  std::size_t message = static_cast<std::size_t>(messageLen);
  if (prefix + message + kLineEndLen >= sizeof(stackBuf)) {
    heapBuf = static_cast<char*>(malloc(prefix + message + kLineEndLen + 1));
    if (heapBuf != nullptr) {
      std::memcpy(heapBuf, stackBuf, prefix);
      vsnprintf(heapBuf + prefix, message + 1, fmt, retryArgs);
      buffer = heapBuf;
    } else {
      message = sizeof(stackBuf) - prefix - kLineEndLen - 1;
    }
  }
  va_end(retryArgs);

  std::memcpy(buffer + prefix + message, kLineEnd, kLineEndLen);
  OpenShock::Serial::Write(reinterpret_cast<const uint8_t*>(buffer), prefix + message + kLineEndLen);

  if (auto tap = s_tap.load(std::memory_order_acquire); tap != nullptr) {
    tap(level, tag, buffer + prefix, message);
  }

  free(heapBuf);
}
