#pragma once

#include "Common.h"
#include "SimpleMutex.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace OpenShock {
  class RateLimiter {
    DISABLE_COPY(RateLimiter);
    DISABLE_MOVE(RateLimiter);

  public:
    RateLimiter();
    ~RateLimiter();

    void addLimit(uint32_t durationMs, uint16_t count);
    void clearLimits();

    bool tryRequest();
    void clearRequests();

    void blockFor(int64_t blockForMs);

  private:
    struct Limit {
      int64_t durationMs;
      uint16_t count;
    };

    OpenShock::SimpleMutex m_mutex;
    int64_t m_nextSlot;
    int64_t m_nextCleanup;
    std::vector<Limit> m_limits;
    std::deque<int64_t> m_requests;
  };
}  // namespace OpenShock
