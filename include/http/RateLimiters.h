#pragma once

#include "RateLimiter.h"

#include <memory>

namespace OpenShock::HTTP::RateLimiters {
  std::shared_ptr<OpenShock::RateLimiter> GetRateLimiter(std::string_view url);
}  // namespace OpenShock::HTTP
