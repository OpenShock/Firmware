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
    {200, 404}
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
    {200, 401}
  );
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(std::string_view hubToken)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/assignLCG", domain.c_str());

  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    uri,
    {
      {     "Accept",                         "application/json"},
      {"DeviceToken", OpenShock::StringToArduinoString(hubToken)}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    {200, 401}
  );
}
