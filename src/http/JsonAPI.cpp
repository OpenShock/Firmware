#include "http/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "Common.h"
#include "Logging.h"
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
  int written = snprintf(uri, sizeof(uri), "https://%s/1/device/pair/%.*s", domain.c_str(), static_cast<int>(accountLinkCode.length()), accountLinkCode.data());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(uri)) {
    OS_LOGE(TAG, "URI truncated for LinkAccount");
    return HTTPError::InternalError;
  }

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
  int written = snprintf(uri, sizeof(uri), "https://%s/1/device/self", domain.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(uri)) {
    OS_LOGE(TAG, "URI truncated for GetHubInfo");
    return HTTPError::InternalError;
  }

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
  int written = snprintf(uri, sizeof(uri), "https://%s/2/device/assignLCG?version=2", domain.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(uri)) {
    OS_LOGE(TAG, "URI truncated for AssignLcg");
    return HTTPError::InternalError;
  }

  HTTP::HTTPClient client(uri);

  client.SetHeader("Accept", "application/json");
  client.SetHeader("DeviceToken", hubToken);

  return client.GetJson<Serialization::JsonAPI::AssignLcgResponse>(Serialization::JsonAPI::ParseAssignLcgJsonResponse);
}
