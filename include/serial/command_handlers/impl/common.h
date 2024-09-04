#pragma once

#include <Arduino.h>

#define SERPR_SYS(format, ...)      Serial.printf("$SYS$|" format "\n", ##__VA_ARGS__)
#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)
