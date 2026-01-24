#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"

#include <esp_system.h>

#include <string>

const char* const TAG = "SerialCmds::CommandHandlers::Domain";

void _handleDomainCommand(std::string_view arg, bool isAutomated) {
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

  // Check if the domain is too long
  // TODO: Remove magic number
  if (arg.length() + 40 >= OPENSHOCK_URI_BUFFER_SIZE) {
    SERPR_ERROR("Domain name too long, please try increasing the \"OPENSHOCK_URI_BUFFER_SIZE\" constant in source code");
    return;
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%.*s/1", arg.length(), arg.data());

  auto resp = OpenShock::HTTP::GetJSON<OpenShock::Serialization::JsonAPI::BackendVersionResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    OpenShock::Serialization::JsonAPI::ParseBackendVersionJsonResponse,
    std::array<uint16_t, 2> {200}
  );

  if (resp.result != OpenShock::HTTP::RequestResult::Success) {
    SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d] (%s), refusing to save domain to config", arg.length(), arg.data(), resp.code, resp.ResultToString());
    return;
  }

  OS_LOGI(TAG, "Successfully connected to \"%.*s\", version: %s, commit: %s, current time: %s", arg.length(), arg.data(), resp.data.version.c_str(), resp.data.commit.c_str(), resp.data.currentTime.c_str());

  bool result = OpenShock::Config::SetBackendDomain(std::string(arg));

  if (!result) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  // Restart to use the new domain
  esp_restart();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::DomainHandler() {
  auto group = OpenShock::SerialCmds::CommandGroup("domain"sv);

  auto& getCommand = group.addCommand("Get the backend domain."sv, _handleDomainCommand);

  auto& setCommand = group.addCommand("Set the backend domain."sv, _handleDomainCommand);
  setCommand.addArgument("domain"sv, "must be a string"sv, "api.shocklink.net"sv);

  return group;
}
