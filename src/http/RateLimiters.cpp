#include "http/RateLimiters.h"

#include "SimpleMutex.h"
#include "util/DomainUtils.h"

#include <string>
#include <unordered_map>

static OpenShock::SimpleMutex s_rateLimitsMutex                                              = {};
static std::unordered_map<std::string, std::shared_ptr<OpenShock::RateLimiter>> s_rateLimits = {};

using namespace OpenShock;

std::shared_ptr<OpenShock::RateLimiter> _rateLimiterFactory(std::string_view domain)
{
  auto rateLimit = std::make_shared<OpenShock::RateLimiter>();

  // Add default limits
  rateLimit->addLimit(1000, 5);        // 5 per second
  rateLimit->addLimit(10 * 1000, 10);  // 10 per 10 seconds

  // per-domain limits
  if (domain == OPENSHOCK_API_DOMAIN) {
    rateLimit->addLimit(60 * 1000, 12);        // 12 per minute
    rateLimit->addLimit(60 * 60 * 1000, 120);  // 120 per hour
  }

  return rateLimit;
}

std::shared_ptr<OpenShock::RateLimiter> HTTP::RateLimiters::GetRateLimiter(std::string_view url)
{
  auto domain = std::string(DomainUtils::GetDomainFromUrl(url));
  if (domain.empty()) {
    return nullptr;
  }

  OpenShock::ScopedLock lock__(&s_rateLimitsMutex);

  auto it = s_rateLimits.find(domain);
  if (it == s_rateLimits.end()) {
    s_rateLimits.emplace(domain, _rateLimiterFactory(domain));
    it = s_rateLimits.find(domain);
  }

  return it->second;
}
