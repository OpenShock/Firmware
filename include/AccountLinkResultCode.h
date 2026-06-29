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
}  // namespace OpenShock
