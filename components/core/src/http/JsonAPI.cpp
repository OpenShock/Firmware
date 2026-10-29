#include <freertos/FreeRTOS.h>

#include "http/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "BackendConfig.h"
#include "Logging.h"
#include "OpenShock.h"
#include "StringHelpers.h"

#include <optional>
#include <string>

using namespace OpenShock;

// "https://<configured backend domain><path>"
[[nodiscard]] static std::optional<std::string> tryGetBackendUri(std::string_view path)
{
  auto domain = Config::GetBackendDomain();
  if (!domain) {
    return std::nullopt;
  }

  std::string uri;
  uri.reserve(8 + domain->size() + path.size());
  uri = "https://";
  uri += *domain;
  uri += path;
  return uri;
}

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(std::string_view accountLinkCode)
{
  auto uri = tryGetBackendUri(std::string("/1/device/pair/") + std::string(accountLinkCode));
  if (!uri) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return HTTP::GetJSON<Serialization::JsonAPI::AccountLinkResponse>(
    *uri,
    {
      {"Accept", "application/json"}
  },
    Serialization::JsonAPI::ParseAccountLinkJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}

HTTP::Response<Serialization::JsonAPI::HubInfoResponse> HTTP::JsonAPI::GetHubInfo(std::string_view hubToken)
{
  auto uri = tryGetBackendUri("/1/device/self");
  if (!uri) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return HTTP::GetJSON<Serialization::JsonAPI::HubInfoResponse>(
    *uri,
    {
      {     "Accept",    "application/json"},
      {"DeviceToken", std::string(hubToken)}
  },
    Serialization::JsonAPI::ParseHubInfoJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(std::string_view hubToken)
{
  auto uri = tryGetBackendUri("/2/device/assignLCG?version=2");
  if (!uri) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    *uri,
    {
      {     "Accept",    "application/json"},
      {"DeviceToken", std::string(hubToken)}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}
