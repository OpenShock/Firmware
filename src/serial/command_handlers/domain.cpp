#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"
#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"

#include <string>

void _handleDomainCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
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
    {200}
  );

  if (resp.result != OpenShock::HTTP::RequestResult::Success) {
    SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d], refusing to save domain to config", arg.length(), arg.data(), resp.code);
    return;
  }

  ESP_LOGI(TAG, "Successfully connected to \"%.*s\", version: %s, commit: %s, current time: %s", arg.length(), arg.data(), resp.data.version.c_str(), resp.data.commit.c_str(), resp.data.currentTime.c_str());

  bool result = OpenShock::Config::SetBackendDomain(arg);

  if (!result) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  // Restart to use the new domain
  ESP.restart();
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::DomainHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "domain"_sv,
    R"(domain
  Get the backend domain.

domain [<domain>]
  Set the backend domain.
  Arguments:
    <domain> must be a string.
  Example:
    domain api.shocklink.net
)",
    _handleDomainCommand,
  };
}
