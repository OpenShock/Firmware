#include <sdkconfig.h>

#if CONFIG_OPENSHOCK_DIAG_SDCARD

#include "serial_console/command_handlers/common.h"

#include "diagnostics/SdCard.h"
#include "StringHelpers.h"

using OpenShock::Diagnostics::SdCard::State;

static void handleStatusCommand(std::string_view arg, bool isAutomated)
{
  auto info = OpenShock::Diagnostics::SdCard::GetInfo();
  SERPR_RESPONSE(
    "SdCard|state=%s,sizeMiB=%lu,epoch=%08lx,nextBlock=%llu,boot=%lu,droppedRecords=%lu,droppedBytes=%lu,nextDump=%llu",
    OpenShock::Diagnostics::SdCard::StateName(info.state),
    static_cast<unsigned long>(info.cardSectors / 2048),
    static_cast<unsigned long>(info.epoch),
    static_cast<unsigned long long>(info.nextSeq),
    static_cast<unsigned long>(info.boot),
    static_cast<unsigned long>(info.droppedRecords),
    static_cast<unsigned long>(info.droppedBytes),
    static_cast<unsigned long long>(info.nextDumpSeq)
  );
}

static void handleFormatCommand(std::string_view arg, bool isAutomated)
{
  if (OpenShock::StringTrim(arg) != "--confirm"sv) {
    SERPR_ERROR("This destroys everything on the card; run `sd format --confirm` to go ahead");
    return;
  }

  auto info = OpenShock::Diagnostics::SdCard::GetInfo();
  if (info.state == State::Disabled || info.state == State::NoCard) {
    SERPR_ERROR("No SD card (%s)", OpenShock::Diagnostics::SdCard::StateName(info.state));
    return;
  }

  if (!OpenShock::Diagnostics::SdCard::Format()) {
    SERPR_ERROR("Formatting failed (%s)", OpenShock::Diagnostics::SdCard::StateName(OpenShock::Diagnostics::SdCard::GetInfo().state));
    return;
  }

  SERPR_SUCCESS("Card formatted and mounted");
}

static void handleEjectCommand(std::string_view arg, bool isAutomated)
{
  if (!OpenShock::Diagnostics::SdCard::Eject()) {
    SERPR_ERROR("Eject failed (%s)", OpenShock::Diagnostics::SdCard::StateName(OpenShock::Diagnostics::SdCard::GetInfo().state));
    return;
  }

  SERPR_SUCCESS("Safe to remove the card");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::SdCardHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("sd"sv);

  group.addCommand("status"sv, "Show the SD card state."sv, handleStatusCommand);
  auto& format = group.addCommand("format"sv, "Format the SD card for logs and crash dumps, destroying its contents."sv, handleFormatCommand);
  format.addArgument("--confirm"sv, "must be --confirm"sv, "--confirm"sv);
  group.addCommand("eject"sv, "Flush and shut down the SD card so it can be removed."sv, handleEjectCommand);

  return group;
}

#endif
