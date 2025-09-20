#pragma once

#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"

#include <string_view>

namespace OpenShock::HTTP::JsonAPI {
  /// @brief Links the hub to the account with the given account link code, returns the hub token. Valid response codes: 200, 404
  /// @param hubToken
  /// @return
  HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> LinkAccount(std::string_view accountLinkCode);

  /// @brief Gets the hub info for the given hub token. Valid response codes: 200, 401
  /// @param hubToken
  /// @return
  HTTP::Response<Serialization::JsonAPI::HubInfoResponse> GetHubInfo(std::string_view hubToken);

  /// @brief Requests a Live Control Gateway to connect to. Valid response codes: 200, 401
  /// @param hubToken
  /// @return
  HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> AssignLcg(std::string_view hubToken);
}  // namespace OpenShock::HTTP::JsonAPI
