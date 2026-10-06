#include "serial_console/command_handlers/common.h"

#include "config/Config.h"
#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"

#include <esp_system.h>

#include <cstdio>
#include <string>

const char* const TAG = "SerialCmds::CommandHandlers::Domain";

static void handleDomainCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    std::string domain;
    if (!OpenShock::Config::GetBackendDomain(domain)) {
      SERPR_ERROR("Failed to get domain from config");
      return;
    }

    // Get domain
    SERPR_RESPONSE("Domain|%s", domain.c_str());
    return;
  }

  // 253 characters is the DNS limit for a full domain name
  if (arg.length() > 253) {
    SERPR_ERROR("Domain name too long (max 253 characters)");
    return;
  }

  std::string uri = "https://" + std::string(arg) + "/1";

  auto resp = OpenShock::HTTP::GetJSON<OpenShock::Serialization::JsonAPI::BackendVersionResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    OpenShock::Serialization::JsonAPI::ParseBackendVersionJsonResponse,
    std::array<uint16_t, 2> {200}
  );

  if (resp.result != OpenShock::HTTP::RequestResult::Success) {
    SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d] (%s), refusing to save domain to config", static_cast<int>(arg.length()), arg.data(), resp.code, resp.ResultToString());
    return;
  }

  OS_LOGI(TAG, "Successfully connected to \"%.*s\", version: %s, commit: %s, current time: %s", static_cast<int>(arg.length()), arg.data(), resp.data.version.c_str(), resp.data.commit.c_str(), resp.data.currentTime.c_str());

  bool result = OpenShock::Config::SetBackendDomain(std::string(arg));

  if (!result) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  // Restart to use the new domain
  esp_restart();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::DomainHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("domain"sv);

  group.addCommand("Get the backend domain."sv, handleDomainCommand);

  auto& setCommand = group.addCommand("Set the backend domain."sv, handleDomainCommand);
  setCommand.addArgument("domain"sv, "must be a string"sv, "api.shocklink.net"sv);

  return group;
}
