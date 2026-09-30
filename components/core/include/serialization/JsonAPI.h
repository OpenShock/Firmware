#pragma once

#include "enums/ShockerModelType.h"
#include "json/Json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OpenShock::Serialization::JsonAPI {
  struct LcgInstanceDetailsResponse {
    std::string name;
    std::string version;
    std::string currentTime;
    std::string countryCode;
    std::string fqdn;
  };
  struct BackendVersionResponse {
    std::string version;
    std::string commit;
    std::string currentTime;
  };
  struct AccountLinkResponse {
    std::string authToken;
  };
  struct HubInfoResponse {
    std::string hubId;
    std::string hubName;
    struct ShockerInfo {
      std::string id;
      uint16_t rfId;
      OpenShock::ShockerModelType model;
    };
    std::vector<ShockerInfo> shockers;
  };
  struct AssignLcgResponse {
    std::string host;
    uint16_t port;
    std::string path;
    std::string country;
  };

  bool ParseLcgInstanceDetailsJsonResponse(int code, JSON::JsonView root, LcgInstanceDetailsResponse& out);
  bool ParseBackendVersionJsonResponse(int code, JSON::JsonView root, BackendVersionResponse& out);
  bool ParseAccountLinkJsonResponse(int code, JSON::JsonView root, AccountLinkResponse& out);
  bool ParseHubInfoJsonResponse(int code, JSON::JsonView root, HubInfoResponse& out);
  bool ParseAssignLcgJsonResponse(int code, JSON::JsonView root, AssignLcgResponse& out);
}  // namespace OpenShock::Serialization::JsonAPI
