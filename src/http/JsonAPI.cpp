#include "http/JsonAPI.h"

#include "Constants.h"

using namespace OpenShock;

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(const char* accountLinkCode) {
  char uri[256];
  sprintf(uri, OPENSHOCK_API_URL("/1/device/pair/%s"), accountLinkCode);

  return HTTP::GetJSON<Serialization::JsonAPI::AccountLinkResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    Serialization::JsonAPI::ParseAccountLinkJsonResponse,
    {200, 404}
  );
}

HTTP::Response<Serialization::JsonAPI::DeviceInfoResponse> HTTP::JsonAPI::GetDeviceInfo(const String& deviceToken) {
  return HTTP::GetJSON<Serialization::JsonAPI::DeviceInfoResponse>(
    OPENSHOCK_API_URL("/1/device/self"),
    {
      {     "Accept", "application/json"},
      {"DeviceToken",        deviceToken}
  },
    Serialization::JsonAPI::ParseDeviceInfoJsonResponse,
    {200, 401}
  );
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(const String& deviceToken) {
  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    OPENSHOCK_API_URL("/1/device/assign_lcg"),
    {
      {     "Accept", "application/json"},
      {"DeviceToken",        deviceToken}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    {200, 401}
  );
}
