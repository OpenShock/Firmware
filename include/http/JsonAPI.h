#pragma once

#include "http/HTTPRequestManager.h"
#include "serialization/JsonAPI.h"

namespace OpenShock::HTTP::JsonAPI {
  HTTP::Response<Serialization::JsonAPI::AccountLinkResponse> LinkAccount(const char* accountLinkCode);
  HTTP::Response<Serialization::JsonAPI::DeviceInfoResponse> GetDeviceInfo(const String& deviceToken);
  HTTP::Response<Serialization::JsonAPI::AssignLcgResponse> AssignLcg(const String& deviceToken);
}  // namespace OpenShock::HTTP::JsonAPI
