#include "serial_console/command_handlers/common.h"

#include "migration/StorageMigration.h"
#include "util/HexUtils.h"
#include "StringHelpers.h"

#include <span>

// `migrate`: the state of the 1.x -> 2.x storage migration (migration/StorageMigration.h) and a way to run its www
// step from an image of your own, for builds that have no release carrying one.

static void handleMigrateStatus(std::string_view arg, bool isAutomated)
{
  (void)arg;
  (void)isAutomated;
  SERPR_RESPONSE("Migrate|%s", OpenShock::Migration::IsPending() ? "pending" : "done");
}

static void handleMigrateNow(std::string_view arg, bool isAutomated)
{
  (void)arg;
  (void)isAutomated;
  if (!OpenShock::Migration::RequestNow()) {
    SERPR_ERROR("No storage migration is pending");
    return;
  }
  SERPR_SUCCESS("Running the next migration step now");
}

static void handleMigrateWww(std::string_view arg, bool isAutomated)
{
  (void)isAutomated;
  auto parts = OpenShock::StringSplit(arg, ' ', 1);
  if (parts.size() != 2 || parts[0].empty() || parts[1].size() != 64) {
    SERPR_ERROR("Usage: migrate www <url> <sha256 hex>");
    return;
  }
  uint8_t sha256[32];
  if (OpenShock::HexUtils::TryParseHex(parts[1], std::span<uint8_t>(sha256)) != sizeof(sha256)) {
    SERPR_ERROR("The hash is not 64 hex digits");
    return;
  }
  if (!OpenShock::Migration::RequestWww(std::string(parts[0]), sha256)) {
    SERPR_ERROR("No storage migration is pending");
    return;
  }
  SERPR_SUCCESS("The www partition migration will download that image once WiFi is up; the hub restarts when it is done");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::MigrateHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("migrate"sv);

  group.addCommand("Whether the 1.x -> 2.x storage migration is still pending"sv, handleMigrateStatus);
  group.addCommand("now"sv, "Run the next pending migration step now instead of after the retry delay"sv, handleMigrateNow);

  auto& www = group.addCommand("www"sv, "Run the www partition migration from this image instead of the running release's"sv, handleMigrateWww);
  www.addArgument("url"sv, "the www image (http or https)"sv, "http://192.168.1.2:8000/www.bin"sv);
  www.addArgument("sha256"sv, "its SHA-256, 64 hex digits"sv, "0000000000000000000000000000000000000000000000000000000000000000"sv);

  return group;
}
