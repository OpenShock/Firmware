#include <freertos/FreeRTOS.h>

#include "RateLimiter.h"

#include "Core.h"

#include <algorithm>

const char* const TAG = "RateLimiter";

OpenShock::RateLimiter::RateLimiter()
  : m_mutex()
  , m_nextSlot(0)
  , m_nextCleanup(0)
  , m_limits()
  , m_requests()
{
}

OpenShock::RateLimiter::~RateLimiter()
{
}

void OpenShock::RateLimiter::addLimit(uint32_t durationMs, uint16_t count)
{
  OpenShock::ScopedLock lock__(&m_mutex);

  // Insert sorted
  m_limits.insert(std::upper_bound(m_limits.begin(), m_limits.end(), durationMs, [](int64_t durationMs, const Limit& limit) { return durationMs < limit.durationMs; }), {durationMs, count});

  m_nextSlot    = 0;
  m_nextCleanup = 0;
}

void OpenShock::RateLimiter::clearLimits()
{
  OpenShock::ScopedLock lock__(&m_mutex);

  m_limits.clear();
  m_nextSlot    = 0;
  m_nextCleanup = 0;
}

bool OpenShock::RateLimiter::tryRequest()
{
  int64_t now = OpenShock::millis();

  OpenShock::ScopedLock lock__(&m_mutex);

  if (m_limits.empty()) {
    return true;
  }
  if (m_requests.empty()) {
    m_requests.push_back(now);
    return true;
  }

  // Cleanup based on longest limit
  if (m_nextCleanup <= now) {
    int64_t longestLimit = m_limits.back().durationMs;
    int64_t expiresAt    = now - longestLimit;

    // erase everything that’s expired
    auto firstAlive = std::upper_bound(m_requests.begin(), m_requests.end(), expiresAt);
    m_requests.erase(m_requests.begin(), firstAlive);

    if (!m_requests.empty()) {
      m_nextCleanup = m_requests.front() + longestLimit;
    } else {
      // nothing to clean until we add a new request
      m_nextCleanup = now + longestLimit;
    }
  }

  if (m_nextSlot > now) {
    return false;
  }

  // Check if we've exceeded any limits, starting from the largest duration
  for (std::size_t i = m_limits.size(); i > 0;) {
    const auto& limit = m_limits[--i];

    // Calculate the window start time
    int64_t windowStart = now - limit.durationMs;

    // Check how many requests are inside the limit window and track earliest in-window element
    std::size_t insideWindow = 0;
    auto it                  = m_requests.rbegin();
    for (; it != m_requests.rend(); ++it) {
      if (*it < windowStart) break;
      ++insideWindow;
    }

    // If the window is full, set the wait time until its available, and reject the request
    if (insideWindow >= limit.count) {
      auto firstInWindow = (it == m_requests.rend()) ? m_requests.begin() : it.base();

      m_nextSlot = *firstInWindow + limit.durationMs;
      return false;
    }
  }

  // Add the request
  m_requests.push_back(now);

  return true;
}
void OpenShock::RateLimiter::clearRequests()
{
  OpenShock::ScopedLock lock__(&m_mutex);

  m_requests.clear();
  m_nextSlot    = 0;
  m_nextCleanup = 0;
}

void OpenShock::RateLimiter::blockFor(int64_t blockForMs)
{
  OpenShock::ScopedLock lock__(&m_mutex);

  int64_t blockUntil = OpenShock::millis() + blockForMs;
  m_nextSlot         = std::max(m_nextSlot, blockUntil);
}
