#include "http/JsonAPI.h"

#include "Common.h"
#include "config/Config.h"

using namespace OpenShock;

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(const char* accountLinkCode) {
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/pair/%s", domain.c_str(), accountLinkCode);

  return HTTP::GetJSON<Serialization::JsonAPI::AccountLinkResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    Serialization::JsonAPI::ParseAccountLinkJsonResponse,
    {200, 404}
  );
}

HTTP::Response<Serialization::JsonAPI::DeviceInfoResponse> HTTP::JsonAPI::GetDeviceInfo(StringView deviceToken) {
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/self", domain.c_str());

  return HTTP::GetJSON<Serialization::JsonAPI::DeviceInfoResponse>(
    uri,
    {
      {     "Accept",            "application/json"},
      {"DeviceToken", deviceToken.toArduinoString()}
  },
    Serialization::JsonAPI::ParseDeviceInfoJsonResponse,
    {200, 401}
  );
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(StringView deviceToken) {
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/assignLCG", domain.c_str());

  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    uri,
    {
      {     "Accept",            "application/json"},
      {"DeviceToken", deviceToken.toArduinoString()}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    {200, 401}
  );
}
