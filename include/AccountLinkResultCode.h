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
  };
}  // namespace OpenShock
