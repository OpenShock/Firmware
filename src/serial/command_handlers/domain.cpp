#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "http/HTTPClient.h"
#include "http/JsonAPI.h"

#include <esp_system.h>

#include <string>

const char* const TAG = "Serial::CommandHandlers::Domain";

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

  OpenShock::HTTP::HTTPClient client(uri);
  auto response = client.GetJson<OpenShock::Serialization::JsonAPI::BackendVersionResponse>(OpenShock::Serialization::JsonAPI::ParseBackendVersionJsonResponse);
  if (!response.Ok() || response.StatusCode() != 200) {
    SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d] (%s), refusing to save domain to config", arg.length(), arg.data(), response.StatusCode(), OpenShock::HTTP::HTTPErrorToString(response.Error()));
    return;
  }

  auto content = response.ReadJson();
  if (content.error != OpenShock::HTTP::HTTPError::None) {
    SERPR_ERROR("Tried to read response from backend, but failed (%s), refusing to save domain to config", OpenShock::HTTP::HTTPErrorToString(response.Error()));
    return;
  }

  OS_LOGI(TAG, "Successfully connected to \"%.*s\", version: %s, commit: %s, current time: %s", arg.length(), arg.data(), content.data.version.c_str(), content.data.commit.c_str(), content.data.currentTime.c_str());

  bool result = OpenShock::Config::SetBackendDomain(std::string(arg));

  if (!result) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  // Restart to use the new domain
  esp_restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::DomainHandler() {
  auto group = OpenShock::Serial::CommandGroup("domain"sv);

  auto& getCommand = group.addCommand("Get the backend domain."sv, _handleDomainCommand);

  auto& setCommand = group.addCommand("Set the backend domain."sv, _handleDomainCommand);
  setCommand.addArgument("domain"sv, "must be a string"sv, "api.shocklink.net"sv);

  return group;
}
