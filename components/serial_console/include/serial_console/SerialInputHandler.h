#pragma once

#include <cstdint>
#include <cstdio>

// Serial console output. TX is written to stdout, which the IDF console driver
// routes to the primary console (UART0) and any configured secondary channel
// (USB-Serial-JTAG / USB-CDC). RX is handled separately by the `serial` transport
// (Serial::Read), which also routes stdout through that same driver.
//
// OS_SERIAL_PRINT takes a runtime string (never a format string, so '%' in the
// data is safe); OS_SERIAL_PRINTF takes a literal format + args; OS_SERIAL_PRINTLN
// takes an optional string literal (the "" concatenation appends the CRLF).
#define OS_SERIAL_PRINT(str)   std::fputs(str, stdout)
#define OS_SERIAL_PRINTF(...)  std::printf(__VA_ARGS__)
#define OS_SERIAL_PRINTLN(...) std::fputs("" __VA_ARGS__ "\r\n", stdout)

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
