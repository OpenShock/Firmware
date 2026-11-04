#pragma once

#include "OpenShock.h"
#include "SimpleMutex.h"
#include "util/ManagedTask.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <vector>

namespace OpenShock {
  /// @brief Plays a looping pattern of LED states on one long-lived task.
  ///
  /// SetPattern() swaps the pattern and wakes the task through a task notification, so a change takes effect right
  /// away and the caller (often the default event loop) never waits for a task to be torn down and recreated.
  /// `State` must have a `uint32_t duration` (milliseconds); the derived driver writes a state to the hardware in
  /// apply(), and puts it to rest in enterSleep() / brings it back in exitSleep(). All three run on the pattern task.
  /// Derived destructors must call stopTask() before releasing anything those use.
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
        m_sleep = false;
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

    /// @brief Turns the LED off and lets its hardware rest (enterSleep), with the task blocked and not waking, until
    ///        the next SetPattern brings it back (exitSleep).
    void Sleep()
    {
      {
        ScopedLock lock__(&m_patternMutex);
        m_pattern.clear();
        m_sleep = true;
        ++m_generation;
      }
      wake();
    }

  protected:
    PatternPlayer() = default;
    virtual ~PatternPlayer() { stopTask(); }

    virtual void apply(const State& state) = 0;
    /// @brief Turns the LED off and stops what drives it, so the hardware rests.
    virtual void enterSleep() = 0;
    /// @brief Undoes enterSleep(); the next apply() then shows a state as usual.
    virtual void exitSleep() = 0;

    /// @brief Whether the hardware is resting. Pattern task only, or after stopTask() (a destructor deciding what to undo).
    bool isAsleep() const { return m_asleep; }

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
      bool sleep          = false;

      while (!m_task.stopRequested()) {
        {
          ScopedLock lock__(&m_patternMutex);
          if (generation != m_generation) {
            pattern    = m_pattern;
            sleep      = m_sleep;
            generation = m_generation;
          }
        }

        if (sleep && !m_asleep) {
          enterSleep();
          m_asleep = true;
        } else if (!pattern.empty() && m_asleep) {
          exitSleep();
          m_asleep = false;
        }

        if (pattern.empty()) {
          TaskUtils::WaitForNotify(portMAX_DELAY);  // Nothing to play until the next SetPattern / stop
          continue;
        }

        for (const State& state : pattern) {
          apply(state);

          // Sleep for the state's duration, but wake immediately on a new pattern or a stop request
          if (TaskUtils::WaitForNotify(pdMS_TO_TICKS(state.duration)) != 0) {
            break;
          }
        }
      }
    }

    SimpleMutex m_patternMutex;
    std::vector<State> m_pattern;   // Guarded by m_patternMutex
    uint32_t m_generation = 0;      // Guarded by m_patternMutex; bumped on every SetPattern and Sleep
    bool m_sleep          = false;  // Guarded by m_patternMutex; Sleep() asked for the hardware to rest
    bool m_asleep         = false;  // Pattern task only: enterSleep() ran and exitSleep() has not

    ManagedTask m_task {"PatternPlayer", "LED pattern task"};
  };
}  // namespace OpenShock
