#include "Logging.h"

#include <cstdarg>
#include <cstdio>

// Forwards OpenShock's OS_LOG* output to the IDF console. vprintf writes to
// stdout, which IDF's console driver routes to the primary UART (UART0) and any
// configured secondary channel (USB-Serial-JTAG / USB-CDC). This keeps logging
// free of any Arduino dependency.
extern "C" int openshock_log_printf(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int ret = vprintf(fmt, args);
  va_end(args);
  return ret;
}
