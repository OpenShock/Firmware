#pragma once

#include <cstdint>

namespace OpenShock {
  enum class AccountLinkResultCode : uint8_t {
    Success              = 0,
    CodeRequired         = 1,
    InvalidCodeLength    = 2,
    NoInternetConnection = 3,
    InvalidCode          = 4,
    RateLimited          = 5,
    InternalError        = 6,
    // More descriptive failure causes (previously all reported as InternalError)
    RequestFailed    = 7,   // Could not start/complete the HTTP request (DNS, TLS, connection refused, ...)
    RequestTimedOut  = 8,   // The request to the backend timed out
    ServerError      = 9,   // The backend returned an unexpected response code
    InvalidResponse  = 10,  // The backend response could not be parsed or was missing the auth token
    ConfigSaveFailed = 11,  // Failed to persist the auth token to flash
  };

  /// @brief The code's name, as used for the captive portal's JSON error codes.
  constexpr const char* AccountLinkResultCodeToString(AccountLinkResultCode code)
  {
    switch (code) {
      case AccountLinkResultCode::Success:
        return "Success";
      case AccountLinkResultCode::CodeRequired:
        return "CodeRequired";
      case AccountLinkResultCode::InvalidCodeLength:
        return "InvalidCodeLength";
      case AccountLinkResultCode::NoInternetConnection:
        return "NoInternetConnection";
      case AccountLinkResultCode::InvalidCode:
        return "InvalidCode";
      case AccountLinkResultCode::RateLimited:
        return "RateLimited";
      case AccountLinkResultCode::RequestFailed:
        return "RequestFailed";
      case AccountLinkResultCode::RequestTimedOut:
        return "RequestTimedOut";
      case AccountLinkResultCode::ServerError:
        return "ServerError";
      case AccountLinkResultCode::InvalidResponse:
        return "InvalidResponse";
      case AccountLinkResultCode::ConfigSaveFailed:
        return "ConfigSaveFailed";
      case AccountLinkResultCode::InternalError:
      default:
        return "InternalError";
    }
  }
}  // namespace OpenShock
