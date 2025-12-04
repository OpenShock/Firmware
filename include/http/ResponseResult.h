#pragma once

#include <cstdint>

namespace OpenShock::HTTP {
  enum class ResponseResult : uint8_t {
    Closed,       // Connection closed
    Success,      // Request completed successfully
    TimedOut,     // Request timed out
    ParseFailed,  // Request completed, but JSON parsing failed
    Cancelled,    // Request was cancelled
  };
}  // namespace OpenShock::HTTP
