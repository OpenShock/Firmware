#include <sdkconfig.h>

#if CONFIG_OPENSHOCK_DIAG

#include "serial_console/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "diagnostics/Diagnostics.h"
#include "enums/DiagnosticsLogLevel.h"
#include "StringHelpers.h"

#if CONFIG_OPENSHOCK_DIAG_UPLOAD
static void handleCoredumpCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto enabled = OpenShock::Config::GetDiagnosticsCoredumpUpload();
    if (!enabled) {
      SERPR_ERROR("Failed to get setting from config");
      return;
    }

    SERPR_RESPONSE("CoredumpUpload|%s", *enabled ? "true" : "false");
    return;
  }

  auto enabled = OpenShock::Convert::ToBool(OpenShock::StringTrim(arg));
  if (!enabled) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  if (!OpenShock::Config::SetDiagnosticsCoredumpUpload(*enabled)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

static void handleLogsCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto level = OpenShock::Config::GetDiagnosticsLogUploadLevel();
    if (!level) {
      SERPR_ERROR("Failed to get setting from config");
      return;
    }

    SERPR_RESPONSE("LogUploadLevel|%s", OpenShock::DiagnosticsLogLevelName(*level));
    return;
  }

  auto level = OpenShock::TryParseDiagnosticsLogLevel(OpenShock::StringTrim(arg));
  if (!level) {
    SERPR_ERROR("Invalid argument (none, error, warn, info, debug or verbose)");
    return;
  }

  if (!OpenShock::Config::SetDiagnosticsLogUploadLevel(*level)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

static void handleUploadCommand(std::string_view arg, bool isAutomated)
{
  OpenShock::Diagnostics::RequestUpload();
  SERPR_SUCCESS("Upload requested");
}
#endif

static void handleStatusCommand(std::string_view arg, bool isAutomated)
{
  auto status = OpenShock::Diagnostics::GetStatus();
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  SERPR_RESPONSE(
    "DiagStatus|coredump=%s,coredumpSize=%zu,coredumpUploaded=%s,coredumpOnCard=%s,pendingLogBytes=%llu,uploading=%s,lastUpload=%s",
    status.coredumpStored ? "true" : "false",
    status.coredumpSize,
    status.coredumpUploaded ? "true" : "false",
    status.coredumpOnCard ? "true" : "false",
    static_cast<unsigned long long>(status.pendingLogBytes),
    status.uploadRunning ? "true" : "false",
    status.lastUploadResult != nullptr ? status.lastUploadResult : "none"
  );
#else
  SERPR_RESPONSE("DiagStatus|coredump=%s,coredumpSize=%zu,coredumpOnCard=%s", status.coredumpStored ? "true" : "false", status.coredumpSize, status.coredumpOnCard ? "true" : "false");
#endif
}

static void handleEraseCommand(std::string_view arg, bool isAutomated)
{
  if (!OpenShock::Diagnostics::EraseCoredump()) {
    SERPR_ERROR("Failed to erase the crash dump");
    return;
  }

  SERPR_SUCCESS("Crash dump erased");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::DiagHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("diag"sv);

#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  group.addCommand("coredump"sv, "Get whether crash dumps are uploaded."sv, handleCoredumpCommand);
  auto& setCoredump = group.addCommand("coredump"sv, "Enable/disable uploading crash dumps."sv, handleCoredumpCommand);
  setCoredump.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  group.addCommand("logs"sv, "Get the least severe log level that is uploaded."sv, handleLogsCommand);
  auto& setLogs = group.addCommand("logs"sv, "Set the least severe log level that is uploaded; none uploads no logs."sv, handleLogsCommand);
  setLogs.addArgument("level"sv, "none, error, warn, info, debug or verbose"sv, "error"sv);

  group.addCommand("upload"sv, "Upload what is pending now, once the backend is connected."sv, handleUploadCommand);
#endif

  group.addCommand("status"sv, "Show the stored crash dump and what has it."sv, handleStatusCommand);
  group.addCommand("erase"sv, "Erase the stored crash dump."sv, handleEraseCommand);

  return group;
}

#endif
