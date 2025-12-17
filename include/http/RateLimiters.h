#pragma once

#include "RateLimiter.h"

#include <memory>
#include <string_view>

namespace OpenShock::HTTP::RateLimiters {
  std::shared_ptr<OpenShock::RateLimiter> GetRateLimiter(std::string_view url);
}  // namespace OpenShock::HTTP
