#include "http/JsonAPI.h"

#include "Common.h"
#include "config/Config.h"
#include "util/StringUtils.h"

using namespace OpenShock;

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(std::string_view accountLinkCode)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/pair/%.*s", domain.c_str(), accountLinkCode.length(), accountLinkCode.data());

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
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/self", domain.c_str());

  return HTTP::GetJSON<Serialization::JsonAPI::HubInfoResponse>(
    uri,
    {
      {     "Accept",                         "application/json"},
      {"DeviceToken", OpenShock::StringToArduinoString(hubToken)}
  },
    Serialization::JsonAPI::ParseHubInfoJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(std::string_view hubToken)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/2/device/assignLCG?version=2", domain.c_str());

  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    uri,
    {
      {     "Accept",                         "application/json"},
      {"DeviceToken", OpenShock::StringToArduinoString(hubToken)}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}
