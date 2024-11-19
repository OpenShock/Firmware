#pragma once

#include "serial/command_handlers/index.h"

#include "Logging.h"

#include <Arduino.h>

namespace OpenShock::Serial::Util {
  void respError(bool isAutomated, const char* format, ...);
}

#define CLEAR_LINE "\r\x1B[K"

using namespace std::string_view_literals;
