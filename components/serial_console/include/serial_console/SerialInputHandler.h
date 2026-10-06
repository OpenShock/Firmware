#pragma once

#include <cstdint>
#include <string_view>

// Serial console output. TX goes through the raw serial transport
// (Serial::Write), the same path the logger uses, never C stdio: the IDF stdout
// VFS rewrites "\n" to "\r\n", which would double the CRLF every console line
// already carries. Each call issues a single Serial::Write, so a console line is
// never split by a concurrent log line, and lines without a trailing newline
// (prompts) are written out immediately.
//
// OS_SERIAL_PRINT takes a runtime string (never a format string, so '%' in the
// data is safe); OS_SERIAL_PRINTF takes a literal format + args; OS_SERIAL_PRINTLN
// takes an optional string literal (the "" concatenation appends the CRLF).
#define OS_SERIAL_PRINT(str)   OpenShock::SerialInputHandler::Print(str)
#define OS_SERIAL_PRINTF(...)  OpenShock::SerialInputHandler::Printf(__VA_ARGS__)
#define OS_SERIAL_PRINTLN(...) OpenShock::SerialInputHandler::Print("" __VA_ARGS__ "\r\n")

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintWelcomeHeader();
  void PrintVersionInfo();

  // Raw console output; use the OS_SERIAL_* macros above.
  void Print(std::string_view str);
  void Printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
}  // namespace OpenShock::SerialInputHandler
