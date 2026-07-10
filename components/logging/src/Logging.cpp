#include "Logging.h"

#include "serial/Serial.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// Forwards OpenShock's OS_LOG* output to the low-level serial transport: format
// the line into a buffer, then write the raw bytes to the console port. This
// bypasses C stdio (no vprintf/stdout); before the serial driver is installed,
// Serial::Write falls back to the ROM serial output so early-boot logs survive.
extern "C" int openshock_log_printf(const char* fmt, ...)
{
  char stackBuf[256];

  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
  va_end(args);

  if (len <= 0) {
    return len;
  }

  // Fits the stack buffer: write it directly.
  if (static_cast<std::size_t>(len) < sizeof(stackBuf)) {
    OpenShock::Serial::Write(reinterpret_cast<const uint8_t*>(stackBuf), static_cast<std::size_t>(len));
    return len;
  }

  // Longer than the stack buffer (e.g. a config dump): format again into a heap
  // buffer so the line isn't truncated. Fall back to the truncated stack buffer
  // if the allocation fails.
  char* heapBuf = static_cast<char*>(malloc(static_cast<std::size_t>(len) + 1));
  if (heapBuf == nullptr) {
    OpenShock::Serial::Write(reinterpret_cast<const uint8_t*>(stackBuf), sizeof(stackBuf) - 1);
    return static_cast<int>(sizeof(stackBuf) - 1);
  }

  va_start(args, fmt);
  vsnprintf(heapBuf, static_cast<std::size_t>(len) + 1, fmt, args);
  va_end(args);

  OpenShock::Serial::Write(reinterpret_cast<const uint8_t*>(heapBuf), static_cast<std::size_t>(len));
  free(heapBuf);
  return len;
}
