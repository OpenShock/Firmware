#pragma once

#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"

namespace OpenShock::HTTP::JsonAPI {
  /// @brief Links the device to the account with the given account link code, returns the device token. Valid response codes: 200, 404
  /// @param deviceToken
  /// @return
  HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> LinkAccount(const char* accountLinkCode);

  /// @brief Gets the device info for the given device token. Valid response codes: 200, 401
  /// @param deviceToken
  /// @return
  HTTP::Response<Serialization::JsonAPI::DeviceInfoResponse> GetDeviceInfo(StringView deviceToken);

  /// @brief Requests a Live Control Gateway to connect to. Valid response codes: 200, 401
  /// @param deviceToken
  /// @return
  HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> AssignLcg(StringView deviceToken);
}  // namespace OpenShock::HTTP::JsonAPI
