#pragma once

#include "ShockerModelType.h"

#include <cJSON.h>

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

  bool ParseLcgInstanceDetailsJsonResponse(const cJSON* root, LcgInstanceDetailsResponse& out);
  bool ParseBackendVersionJsonResponse(const cJSON* root, BackendVersionResponse& out);
  bool ParseAccountLinkJsonResponse(const cJSON* root, AccountLinkResponse& out);
  bool ParseHubInfoJsonResponse(const cJSON* root, HubInfoResponse& out);
  bool ParseAssignLcgJsonResponse(const cJSON* root, AssignLcgResponse& out);
}  // namespace OpenShock::Serialization::JsonAPI
