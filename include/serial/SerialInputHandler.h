#pragma once

#include <cstdint>

// Making sense of the ESP32 Arduino core #defines
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  #define OS_SERIAL     ::Serial0
  #define OS_SERIAL_USB ::Serial
#elif ARDUINO_USB_MODE
  #define OS_SERIAL     ::Serial
  #define OS_SERIAL_USB ::USBSerial
#else
  #define OS_SERIAL     ::Serial
  // Variant lacks USB Serial
#endif

#if ARDUINO_USB_MODE
  #define OS_SERIAL_PRINT(...) { OS_SERIAL.print(__VA_ARGS__); OS_SERIAL_USB.print(__VA_ARGS__); }
  #define OS_SERIAL_PRINTF(...) { OS_SERIAL.printf(__VA_ARGS__); OS_SERIAL_USB.printf(__VA_ARGS__); }
  #define OS_SERIAL_PRINTLN(...) { OS_SERIAL.println(__VA_ARGS__); OS_SERIAL_USB.println(__VA_ARGS__); }
#else
  #define OS_SERIAL_PRINT(...) OS_SERIAL.print(__VA_ARGS__)
  #define OS_SERIAL_PRINTF(...) OS_SERIAL.printf(__VA_ARGS__)
  #define OS_SERIAL_PRINTLN(...) OS_SERIAL.println(__VA_ARGS__)
#endif

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
