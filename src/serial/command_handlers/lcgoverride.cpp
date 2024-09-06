#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"
#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"
#include "StringView.h"

void _handleLcgOverrideCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
    std::string lcgOverride;
    if (!OpenShock::Config::GetBackendLCGOverride(lcgOverride)) {
      SERPR_ERROR("Failed to get LCG override from config");
      return;
    }

    // Get LCG override
    SERPR_RESPONSE("LcgOverride|%s", lcgOverride.c_str());
    return;
  }

  if (arg.startsWith("clear")) {
    if (arg.size() != 5) {
      SERPR_ERROR("Invalid command (clear command should not have any arguments)");
      return;
    }

    bool result = OpenShock::Config::SetBackendLCGOverride(std::string());
    if (result) {
      SERPR_SUCCESS("Cleared LCG override");
    } else {
      SERPR_ERROR("Failed to clear LCG override");
    }
    return;
  }

  if (arg.startsWith("set ")) {
    if (arg.size() <= 4) {
      SERPR_ERROR("Invalid command (set command should have an argument)");
      return;
    }

    OpenShock::StringView domain = arg.substr(4);

    if (domain.size() + 40 >= OPENSHOCK_URI_BUFFER_SIZE) {
      SERPR_ERROR("Domain name too long, please try increasing the \"OPENSHOCK_URI_BUFFER_SIZE\" constant in source code");
      return;
    }

    char uri[OPENSHOCK_URI_BUFFER_SIZE];
    sprintf(uri, "https://%.*s/1", static_cast<int>(domain.size()), domain.data());

    auto resp = OpenShock::HTTP::GetJSON<OpenShock::Serialization::JsonAPI::LcgInstanceDetailsResponse>(
      uri,
      {
        {"Accept", "application/json"}
    },
      OpenShock::Serialization::JsonAPI::ParseLcgInstanceDetailsJsonResponse,
      {200}
    );

    if (resp.result != OpenShock::HTTP::RequestResult::Success) {
      SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d], refusing to save domain to config", domain.size(), domain.data(), resp.code);
      return;
    }

    OS_LOGI(
      TAG,
      "Successfully connected to \"%.*s\", name: %s, version: %s, current time: %s, country code: %s, FQDN: %s",
      domain.size(),
      domain.data(),
      resp.data.name.c_str(),
      resp.data.version.c_str(),
      resp.data.currentTime.c_str(),
      resp.data.countryCode.c_str(),
      resp.data.fqdn.c_str()
    );

    bool result = OpenShock::Config::SetBackendLCGOverride(domain);

    if (result) {
      SERPR_SUCCESS("Saved config");
    } else {
      SERPR_ERROR("Failed to save config");
    }
    return;
  }

  SERPR_ERROR("Invalid subcommand");
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::LcgOverrideHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "lcgoverride",
    R"(lcgoverride
  Get the domain overridden for LCG endpoint (if any).

lcgoverride set <domain>
  Set a domain to override the LCG endpoint.
  Arguments:
    <domain> must be a string.
  Example:
    lcgoverride set eu1-gateway.shocklink.net

lcgoverride clear
  Clear the overridden LCG endpoint.
  Example:
    lcgoverride clear
)",
    _handleLcgOverrideCommand,
  };
}
