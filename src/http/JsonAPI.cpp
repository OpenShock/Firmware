#include "http/JsonAPI.h"

#include "Common.h"
#include "config/Config.h"
#include "http/HTTPClient.h"
#include "util/StringUtils.h"

using namespace OpenShock;

HTTP::JsonResponse<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(std::string_view accountLinkCode)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return HTTPError::InternalError;
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/pair/%.*s", domain.c_str(), accountLinkCode.length(), accountLinkCode.data());

  HTTP::HTTPClient client(uri);

  client.SetHeader("Accept", "application/json");

  return client.GetJson<Serialization::JsonAPI::AccountLinkResponse>(Serialization::JsonAPI::ParseAccountLinkJsonResponse);
}

HTTP::JsonResponse<Serialization::JsonAPI::HubInfoResponse> HTTP::JsonAPI::GetHubInfo(const char* hubToken)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return HTTPError::InternalError;
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/self", domain.c_str());

  HTTP::HTTPClient client(uri);

  client.SetHeader("Accept", "application/json");
  client.SetHeader("DeviceToken", hubToken);

  return client.GetJson<Serialization::JsonAPI::HubInfoResponse>(Serialization::JsonAPI::ParseHubInfoJsonResponse);
}

HTTP::JsonResponse<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(const char* hubToken)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return HTTPError::InternalError;
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/2/device/assignLCG?version=2", domain.c_str());

  HTTP::HTTPClient client(uri);

  client.SetHeader("Accept", "application/json");
  client.SetHeader("DeviceToken", hubToken);

  return client.GetJson<Serialization::JsonAPI::AssignLcgResponse>(Serialization::JsonAPI::ParseAssignLcgJsonResponse);
}
