#pragma once

#include "enums/ShockerModelType.h"
#include "json/Json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OpenShock::Serialization::JsonAPI {
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

  [[nodiscard]] std::optional<BackendVersionResponse> ParseBackendVersionJsonResponse(int code, JSON::JsonView root);
  [[nodiscard]] std::optional<AccountLinkResponse> ParseAccountLinkJsonResponse(int code, JSON::JsonView root);
  [[nodiscard]] std::optional<HubInfoResponse> ParseHubInfoJsonResponse(int code, JSON::JsonView root);
  [[nodiscard]] std::optional<AssignLcgResponse> ParseAssignLcgJsonResponse(int code, JSON::JsonView root);
}  // namespace OpenShock::Serialization::JsonAPI
