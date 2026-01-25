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


namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
