#include "Logging.h"

#include "serial/Serial.h"

#include <cstdarg>

// Forwards OpenShock's OS_LOG* output to the low-level serial transport, which formats the line and writes the raw
// bytes to the console port. This bypasses C stdio (no vprintf/stdout); before the serial driver is installed,
// Serial::Write falls back to the ROM serial output so early-boot logs survive.
extern "C" int openshock_log_printf(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int len = OpenShock::Serial::VWritef(fmt, args);
  va_end(args);

  return len;
}
