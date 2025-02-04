#pragma once

#include "serial/command_handlers/index.h"

#include "Logging.h"

#include <cstdio>

#define SERPR_SYS(format, ...)      printf("$SYS$|" format "\n", ##__VA_ARGS__)
#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace std::string_view_literals;
