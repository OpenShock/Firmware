#pragma once

#include "serial/command_handlers/index.h"
#include "serial/SerialInputHandler.h"

#include "Logging.h"

#include <Arduino.h>

#if ARDUINO_USB_MODE 
#define SERPR_SYS(format, ...)      OS_SERIAL.printf("$SYS$|" format "\r\n", ##__VA_ARGS__); OS_SERIAL_USB.printf("$SYS$|" format "\r\n", ##__VA_ARGS__);
#else
#define SERPR_SYS(format, ...)      OS_SERIAL.printf("$SYS$|" format "\r\n", ##__VA_ARGS__)
#endif

#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace std::string_view_literals;
