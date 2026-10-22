#pragma once

#include <algorithm>
#include <cstdint>

namespace OpenShock {
  /// @brief When the next attempt at something may be made, backing off after failures.
  ///
  /// Each backOff() pushes the next attempt out by a delay that starts at `initialMs` and doubles up to `maxMs`
  /// (equal values give a fixed hold-off); reset() allows the next attempt immediately. Times are in milliseconds
  /// (OpenShock::millis()). Not thread-safe: owned by one task.
  class Backoff {
  public:
    constexpr Backoff(int64_t initialMs, int64_t maxMs)
      : m_initialMs(initialMs)
      , m_maxMs(maxMs)
    {
    }

    bool ready(int64_t nowMs) const { return nowMs >= m_nextAttemptMs; }

    void backOff(int64_t nowMs)
    {
      m_delayMs       = m_delayMs == 0 ? m_initialMs : std::min(m_delayMs * 2, m_maxMs);
      m_nextAttemptMs = nowMs + m_delayMs;
    }

    void reset()
    {
      m_delayMs       = 0;
      m_nextAttemptMs = 0;
    }

  private:
    int64_t m_initialMs;
    int64_t m_maxMs;
    int64_t m_delayMs       = 0;
    int64_t m_nextAttemptMs = 0;
  };
}  // namespace OpenShock
