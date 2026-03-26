#include "http/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "Common.h"
#include "Logging.h"
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
  int written = snprintf(uri, sizeof(uri), "https://%s/1/device/pair/%.*s", domain.c_str(), static_cast<int>(accountLinkCode.length()), accountLinkCode.data());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(uri)) {
    OS_LOGE(TAG, "URI truncated for LinkAccount");
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
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  int written = snprintf(uri, sizeof(uri), "https://%s/1/device/self", domain.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(uri)) {
    OS_LOGE(TAG, "URI truncated for GetHubInfo");
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

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
  int written = snprintf(uri, sizeof(uri), "https://%s/2/device/assignLCG?version=2", domain.c_str());
  if (written < 0 || static_cast<size_t>(written) >= sizeof(uri)) {
    OS_LOGE(TAG, "URI truncated for AssignLcg");
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

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
