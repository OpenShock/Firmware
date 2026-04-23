#pragma once

#include <Arduino.h>

#include <cstdint>

// Arduino-ESP32 3.x only declares the USB CDC global (HWCDCSerial or USBSerial) when
// ARDUINO_USB_CDC_ON_BOOT=1. When CDC is on boot, `Serial` macro-aliases to that global
// and UART0 remains available as Serial0. When CDC is off boot, `Serial` is UART0 and
// no USB CDC global is instantiated.
#if ARDUINO_USB_CDC_ON_BOOT
#define OS_SERIAL     ::Serial0
#define OS_SERIAL_USB ::Serial  // expands to HWCDCSerial or USBSerial
#define OS_HAS_USB_SERIAL 1
#else
#define OS_SERIAL ::Serial  // expands to Serial0
// No USB serial active on boot
#endif

#if OS_HAS_USB_SERIAL
#define OS_SERIAL_PRINT(...)          \
  {                                   \
    OS_SERIAL.print(__VA_ARGS__);     \
    OS_SERIAL_USB.print(__VA_ARGS__); \
  }
#define OS_SERIAL_PRINTF(...)          \
  {                                    \
    OS_SERIAL.printf(__VA_ARGS__);     \
    OS_SERIAL_USB.printf(__VA_ARGS__); \
  }
#define OS_SERIAL_PRINTLN(...)          \
  {                                     \
    OS_SERIAL.println(__VA_ARGS__);     \
    OS_SERIAL_USB.println(__VA_ARGS__); \
  }
#else
#define OS_SERIAL_PRINT(...)   OS_SERIAL.print(__VA_ARGS__)
#define OS_SERIAL_PRINTF(...)  OS_SERIAL.printf(__VA_ARGS__)
#define OS_SERIAL_PRINTLN(...) OS_SERIAL.println(__VA_ARGS__)
#endif

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
