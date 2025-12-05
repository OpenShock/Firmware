#pragma once

#include "http/HTTPClient.h"
#include "http/JsonResponse.h"
#include "serialization/JsonAPI.h"

#include <string_view>

namespace OpenShock::HTTP::JsonAPI {
  /// @brief Links the hub to the account with the given account link code, returns the hub token. Valid response codes: 200, 404
  /// @param hubToken
  /// @return
  JsonResponse<Serialization::JsonAPI::AccountLinkResponse> LinkAccount(HTTP::HTTPClient& client, std::string_view accountLinkCode);

  /// @brief Gets the hub info for the given hub token. Valid response codes: 200, 401
  /// @param hubToken
  /// @return
  JsonResponse<Serialization::JsonAPI::HubInfoResponse> GetHubInfo(HTTP::HTTPClient& client, const char* hubToken);

  /// @brief Requests a Live Control Gateway to connect to. Valid response codes: 200, 401
  /// @param hubToken
  /// @return
  JsonResponse<Serialization::JsonAPI::AssignLcgResponse> AssignLcg(HTTP::HTTPClient& client, const char* hubToken);
}  // namespace OpenShock::HTTP::JsonAPI
