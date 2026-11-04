#pragma once

#include "OpenShock.h"
#include "SimpleMutex.h"
#include "util/ManagedTask.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace OpenShock {
  /// @brief Plays a looping pattern of LED states on one long-lived task.
  ///
  /// SetPattern() swaps the pattern and wakes the task through a task notification, so a change takes effect right
  /// away and the caller (often the default event loop) never waits for a task to be torn down and recreated.
  /// `State` must have a `uint32_t duration` (milliseconds); the derived driver writes a state to the hardware in
  /// apply(). Derived destructors must call stopTask() before releasing anything apply() uses.
  template<typename State>
  class PatternPlayer {
    DISABLE_COPY(PatternPlayer);
    DISABLE_MOVE(PatternPlayer);

  public:
    void SetPattern(const State* pattern, std::size_t patternLength)
    {
      {
        ScopedLock lock__(&m_patternMutex);
        m_pattern.assign(pattern, pattern + patternLength);
        ++m_generation;
      }
      wake();
    }
    template<std::size_t N>
    inline void SetPattern(const State (&pattern)[N])
    {
      SetPattern(pattern, N);
    }

    /// @brief Stops playing; the LED keeps its last state.
    void ClearPattern() { SetPattern(nullptr, 0); }

  protected:
    PatternPlayer() = default;
    virtual ~PatternPlayer() { stopTask(); }

    virtual void apply(const State& state) = 0;

    /// @brief How often refresh() should run while the current state holds, in milliseconds; 0 for never. Asked after
    ///        apply() and after each refresh(), on the pattern task, so a driver can refresh only while it needs to.
    virtual uint32_t refreshIntervalMs() const { return 0; }
    /// @brief Re-sends the current state (temporal dithering, for one). Runs on the pattern task between apply()s.
    virtual void refresh() { }

    bool startTask(const char* name, uint32_t stackSize)
    {
      return m_task.startExpensive(name, stackSize, 1, [this] { run(); });
    }

    void stopTask() { m_task.stop(); }

  private:
    void wake() { m_task.notify(); }

    void run()
    {
      std::vector<State> pattern;
      uint32_t generation = 0;

      while (!m_task.stopRequested()) {
        {
          ScopedLock lock__(&m_patternMutex);
          if (generation != m_generation) {
            pattern    = m_pattern;
            generation = m_generation;
          }
        }

        if (pattern.empty()) {
          TaskUtils::WaitForNotify(portMAX_DELAY);  // Nothing to play until the next SetPattern / stop
          continue;
        }

        for (const State& state : pattern) {
          apply(state);

          // Hold the state for its duration, refreshing it as often as the driver asks, but wake immediately on a
          // new pattern or a stop request
          if (holdState(state.duration)) {
            break;
          }
        }
      }
    }

    // Returns true when woken by a new pattern or a stop request
    bool holdState(uint32_t durationMs)
    {
      const TickType_t start    = xTaskGetTickCount();
      const TickType_t duration = pdMS_TO_TICKS(durationMs);
      while (true) {
        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= duration) {
          return false;
        }
        TickType_t wait     = duration - elapsed;
        uint32_t intervalMs = refreshIntervalMs();
        if (intervalMs != 0) {
          wait = std::min(wait, std::max<TickType_t>(pdMS_TO_TICKS(intervalMs), 1));
        }
        if (TaskUtils::WaitForNotify(wait) != 0) {
          return true;
        }
        if (intervalMs != 0) {
          refresh();
        }
      }
    }

    SimpleMutex m_patternMutex;
    std::vector<State> m_pattern;  // Guarded by m_patternMutex
    uint32_t m_generation = 0;     // Guarded by m_patternMutex; bumped on every SetPattern

    ManagedTask m_task {"PatternPlayer", "LED pattern task"};
  };
}  // namespace OpenShock
