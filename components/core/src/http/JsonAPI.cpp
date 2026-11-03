#include <freertos/FreeRTOS.h>

#include "http/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "BackendConfig.h"
#include "http/ContentTypes.h"
#include "Logging.h"
#include "OpenShock.h"
#include "StringHelpers.h"

#include <string>

using namespace OpenShock;

// "https://<configured backend domain><path><suffix>", NUL-terminated for the HTTP client
[[nodiscard]] static std::string backendUri(std::string_view path, std::string_view suffix = {})
{
  constexpr std::string_view kScheme = "https://";

  const std::string domain = Config::GetBackendDomain();
  std::string uri;
  uri.reserve(kScheme.size() + domain.size() + path.size() + suffix.size());
  uri.append(kScheme).append(domain).append(path).append(suffix);
  return uri;
}

HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> HTTP::JsonAPI::LinkAccount(std::string_view accountLinkCode)
{
  const std::string uri = backendUri("/1/device/pair/", accountLinkCode);

  return HTTP::GetJSON<Serialization::JsonAPI::AccountLinkResponse>(
    uri.c_str(),
    {
      {"Accept", ContentType::JSON}
  },
    Serialization::JsonAPI::ParseAccountLinkJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}

HTTP::Response<Serialization::JsonAPI::HubInfoResponse> HTTP::JsonAPI::GetHubInfo(const char* hubToken)
{
  const std::string uri = backendUri("/1/device/self");

  return HTTP::GetJSON<Serialization::JsonAPI::HubInfoResponse>(
    uri.c_str(),
    {
      {     "Accept", ContentType::JSON},
      {"DeviceToken",          hubToken}
  },
    Serialization::JsonAPI::ParseHubInfoJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}

HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> HTTP::JsonAPI::AssignLcg(const char* hubToken)
{
  const std::string uri = backendUri("/2/device/assignLCG?version=2");

  return HTTP::GetJSON<Serialization::JsonAPI::AssignLcgResponse>(
    uri.c_str(),
    {
      {     "Accept", ContentType::JSON},
      {"DeviceToken",          hubToken}
  },
    Serialization::JsonAPI::ParseAssignLcgJsonResponse,
    std::array<uint16_t, 2> {200}
  );
}
