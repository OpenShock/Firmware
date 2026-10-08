#include "http/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "config/Config.h"
#include "Logging.h"
#include "OpenShock.h"
#include "StringHelpers.h"

using namespace OpenShock;

// "https://<configured backend domain><path>"
static bool tryGetBackendUri(std::string& uri, std::string_view path)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return false;
  }

  uri.reserve(8 + domain.size() + path.size());
  uri = "https://";
  uri += domain;
  uri += path;
  return true;
}

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(std::string_view accountLinkCode)
{
  std::string uri;
  if (!tryGetBackendUri(uri, std::string("/1/device/pair/") + std::string(accountLinkCode))) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return HTTP::GetJSON<Serialization::JsonAPI::AccountLinkResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    Serialization::JsonAPI::ParseAccountLinkJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}

HTTP::Response<Serialization::JsonAPI::HubInfoResponse> HTTP::JsonAPI::GetHubInfo(std::string_view hubToken)
{
  std::string uri;
  if (!tryGetBackendUri(uri, "/1/device/self")) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return HTTP::GetJSON<Serialization::JsonAPI::HubInfoResponse>(
    uri,
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
  std::string uri;
  if (!tryGetBackendUri(uri, "/2/device/assignLCG?version=2")) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    uri,
    {
      {     "Accept",    "application/json"},
      {"DeviceToken", std::string(hubToken)}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}
