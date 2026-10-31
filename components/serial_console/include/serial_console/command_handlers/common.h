#pragma once

#include "serial_console/command_handlers/index.h"
#include "serial_console/SerialInputHandler.h"

#include "Logging.h"

#define SERPR_SYS(format, ...) OS_SERIAL_PRINTF("$SYS$|" format "\r\n", ##__VA_ARGS__)

#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace std::string_view_literals;

namespace OpenShock::SerialCmds {
  /// @brief The tail of every handler that saved a setting: Success or Error for the save's result.
  void ReportSaved(bool saved);
  /// @brief ReportSaved for a setting the hub has to restart to apply: a successful save restarts the hub.
  void ReportSavedAndRestart(bool saved);
}  // namespace OpenShock::SerialCmds
