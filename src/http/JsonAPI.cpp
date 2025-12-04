#include "http/JsonAPI.h"

#include "Common.h"
#include "config/Config.h"
#include "util/StringUtils.h"

using namespace OpenShock;

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(HTTP::HTTPClient& client, std::string_view accountLinkCode)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/pair/%.*s", domain.c_str(), accountLinkCode.length(), accountLinkCode.data());

  client.SetHeader("Accept", "application/json");

  esp_err_t err = client.Get(uri);
  if (err != ESP_OK) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  if (client.StatusCode() != 200) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return client.ReadResponseJSON(Serialization::JsonAPI::ParseAccountLinkJsonResponse);
}

HTTP::Response<Serialization::JsonAPI::HubInfoResponse> HTTP::JsonAPI::GetHubInfo(HTTP::HTTPClient& client, const char* hubToken)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/1/device/self", domain.c_str());

  client.SetHeader("Accept", "application/json");
  client.SetHeader("DeviceToken", hubToken);

  esp_err_t err = client.Get(uri);
  if (err != ESP_OK) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  if (client.StatusCode() != 200) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return client.ReadResponseJSON(Serialization::JsonAPI::ParseHubInfoJsonResponse);
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(HTTP::HTTPClient& client, const char* hubToken)
{
  std::string domain;
  if (!Config::GetBackendDomain(domain)) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%s/2/device/assignLCG?version=2", domain.c_str());

  client.SetHeader("Accept", "application/json");
  client.SetHeader("DeviceToken", hubToken);

  esp_err_t err = client.Get(uri);
  if (err != ESP_OK) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  if (client.StatusCode() != 200) {
    return {HTTP::RequestResult::InternalError, 0, {}};
  }

  return client.ReadResponseJSON(Serialization::JsonAPI::ParseAssignLcgJsonResponse);
}
