#pragma once

#include "serial/command_handlers/index.h"

#include "Logging.h"

#include <Arduino.h>

#if ARDUINO_USB_MODE 
#define SERPR_SYS(format, ...)      ::Serial.printf("$SYS$|" format "\r\n", ##__VA_ARGS__); ::USBSerial.printf("$SYS$|" format "\r\n", ##__VA_ARGS__);
#else
#define SERPR_SYS(format, ...)      ::Serial.printf("$SYS$|" format "\r\n", ##__VA_ARGS__)
#endif

#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace std::string_view_literals;
