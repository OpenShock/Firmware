#include "serial_console/command_handlers/common.h"

#include "Convert.h"
#include "diagnostics/Diagnostics.h"
#include "StringHelpers.h"

static void handleCoredumpCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("CoredumpUpload|%s", OpenShock::Diagnostics::IsCoredumpUploadEnabled() ? "true" : "false");
    return;
  }

  auto enabled = OpenShock::Convert::ToBool(OpenShock::StringTrim(arg));
  if (!enabled) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  if (!OpenShock::Diagnostics::SetCoredumpUploadEnabled(*enabled)) {
    SERPR_ERROR("Failed to save setting");
    return;
  }

  SERPR_SUCCESS("Saved setting");
}

static void handleLogsCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("LogUploadLevel|%s", OpenShock::Diagnostics::LogLevelName(OpenShock::Diagnostics::GetLogUploadLevel()));
    return;
  }

  OpenShock::Diagnostics::LogLevel level;
  if (!OpenShock::Diagnostics::TryParseLogLevel(OpenShock::StringTrim(arg), level)) {
    SERPR_ERROR("Invalid argument (none, error, warn, info, debug or verbose)");
    return;
  }

  if (!OpenShock::Diagnostics::SetLogUploadLevel(level)) {
    SERPR_ERROR("Failed to save setting");
    return;
  }

  SERPR_SUCCESS("Saved setting");
}

static void handleDomainCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    std::string domain = OpenShock::Diagnostics::GetUploadDomain();
    SERPR_RESPONSE("UploadDomain|%s", domain.empty() ? "backend" : domain.c_str());
    return;
  }

  std::string_view domain = OpenShock::StringTrim(arg);
  if (domain == "backend"sv) {
    domain = {};
  }
  if (!OpenShock::Diagnostics::SetUploadDomain(domain)) {
    SERPR_ERROR("Invalid domain, or failed to save it");
    return;
  }

  SERPR_SUCCESS("Saved setting");
}

static void handleStatusCommand(std::string_view arg, bool isAutomated)
{
  auto status = OpenShock::Diagnostics::GetStatus();
  SERPR_RESPONSE(
    "DiagStatus|coredump=%s,coredumpSize=%zu,coredumpUploaded=%s,coredumpOnCard=%s,pendingLogBytes=%llu,uploadsAllowed=%s,uploading=%s,lastUpload=%s",
    status.coredumpStored ? "true" : "false",
    status.coredumpSize,
    status.coredumpUploaded ? "true" : "false",
    status.coredumpOnCard ? "true" : "false",
    static_cast<unsigned long long>(status.pendingLogBytes),
    status.uploadsAllowed ? "true" : "false",
    status.uploadRunning ? "true" : "false",
    status.lastUploadResult != nullptr ? status.lastUploadResult : "none"
  );
}

static void handleUploadCommand(std::string_view arg, bool isAutomated)
{
  if (!OpenShock::Diagnostics::IsCoredumpUploadEnabled() && OpenShock::Diagnostics::GetLogUploadLevel() == OpenShock::Diagnostics::LogLevel::None) {
    SERPR_ERROR("Uploads are disabled (diag coredump true / diag logs <level>)");
    return;
  }

  if (!OpenShock::Diagnostics::UploadsAllowed()) {
    SERPR_ERROR("Not official firmware: uploads go only to an upload domain (diag domain)");
    return;
  }

  OpenShock::Diagnostics::RequestUpload();
  SERPR_SUCCESS("Upload requested");
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

  group.addCommand("coredump"sv, "Get whether crash dumps are uploaded to the backend."sv, handleCoredumpCommand);
  auto& setCoredump = group.addCommand("coredump"sv, "Enable/disable uploading crash dumps to the backend."sv, handleCoredumpCommand);
  setCoredump.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  group.addCommand("logs"sv, "Get the least severe log level uploaded to the backend."sv, handleLogsCommand);
  auto& setLogs = group.addCommand("logs"sv, "Set the least severe log level uploaded to the backend; none uploads no logs."sv, handleLogsCommand);
  setLogs.addArgument("level"sv, "none, error, warn, info, debug or verbose"sv, "error"sv);

  group.addCommand("domain"sv, "Get where uploads go: the linked backend, or an override domain."sv, handleDomainCommand);
  auto& setDomain = group.addCommand("domain"sv, "Upload to this domain instead of the linked backend (which then gets no hub token); `backend` clears it."sv, handleDomainCommand);
  setDomain.addArgument("domain"sv, "a host name, optionally with :port, or backend"sv, "diagnostics.example.com"sv);

  group.addCommand("status"sv, "Show the stored crash dump and pending logs."sv, handleStatusCommand);
  group.addCommand("upload"sv, "Upload what is pending now, once the backend is connected."sv, handleUploadCommand);
  group.addCommand("erase"sv, "Erase the stored crash dump."sv, handleEraseCommand);

  return group;
}
