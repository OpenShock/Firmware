#include <freertos/FreeRTOS.h>

#include "RateLimiter.h"

#include "Time.h"

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
  m_mutex.lock(portMAX_DELAY);

  // Insert sorted
  m_limits.insert(std::upper_bound(m_limits.begin(), m_limits.end(), durationMs, [](int64_t durationMs, const Limit& limit) { return durationMs < limit.durationMs; }), {durationMs, count});

  m_nextSlot    = 0;
  m_nextCleanup = 0;

  m_mutex.unlock();
}

void OpenShock::RateLimiter::clearLimits()
{
  m_mutex.lock(portMAX_DELAY);

  m_limits.clear();

  m_mutex.unlock();
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

  if (m_nextCleanup <= now) {
    int64_t longestLimit = m_limits.back().durationMs;
    int64_t expiresAt    = now - longestLimit;

    auto nextToExpire = std::find_if(m_requests.begin(), m_requests.end(), [expiresAt](int64_t requestedAtMs) { return requestedAtMs > expiresAt; });
    if (nextToExpire != m_requests.end()) {
      m_requests.erase(m_requests.begin(), nextToExpire);
    }

    m_nextCleanup = m_requests.front() + longestLimit;
  }

  if (m_nextSlot > now) {
    return false;
  }

  // Check if we've exceeded any limits, starting with the highest limit first
  for (std::size_t i = m_limits.size(); i > 0;) {
    const auto& limit = m_limits[--i];

    // Calculate the window start time
    int64_t windowStart = now - limit.durationMs;

    // Check how many requests are inside the limit window
    std::size_t insideWindow = 0;
    for (int64_t request : m_requests) {
      if (request > windowStart) {
        insideWindow++;
      }
    }

    // If the window is full, set the wait time until its available, and reject the request
    if (insideWindow >= limit.count) {
      m_nextSlot = m_requests.back() + limit.durationMs;
      return false;
    }
  }

  // Add the request
  m_requests.push_back(now);

  return true;
}
void OpenShock::RateLimiter::clearRequests()
{
  m_mutex.lock(portMAX_DELAY);

  m_requests.clear();

  m_mutex.unlock();
}

void OpenShock::RateLimiter::blockFor(int64_t blockForMs)
{
  int64_t blockUntil = OpenShock::millis() + blockForMs;

  m_mutex.lock(portMAX_DELAY);

  m_nextSlot = std::max(m_nextSlot, blockUntil);

  m_mutex.unlock();
}
