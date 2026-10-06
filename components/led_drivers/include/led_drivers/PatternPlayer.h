#pragma once

#include "OpenShock.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
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

    bool startTask(const char* name, uint32_t stackSize)
    {
      m_stopRequested.store(false);
      m_taskExited.store(false);
      if (TaskUtils::TaskCreateExpensive(&PatternPlayer::taskEntry, name, stackSize, this, 1, &m_taskHandle) != pdPASS) {
        m_taskHandle = nullptr;
        return false;
      }
      return true;
    }

    void stopTask()
    {
      if (m_taskHandle == nullptr) {
        return;
      }
      m_stopRequested.store(true);
      wake();
      TaskUtils::StopTask(m_taskHandle, m_taskExited, "PatternPlayer", "LED pattern task");
      m_taskHandle = nullptr;
    }

  private:
    void wake()
    {
      if (m_taskHandle != nullptr) {
        xTaskNotifyGive(m_taskHandle);
      }
    }

    static void taskEntry(void* arg) { static_cast<PatternPlayer*>(arg)->run(); }

    void run()
    {
      std::vector<State> pattern;
      uint32_t generation = 0;

      while (!m_stopRequested.load()) {
        {
          ScopedLock lock__(&m_patternMutex);
          if (generation != m_generation) {
            pattern    = m_pattern;
            generation = m_generation;
          }
        }

        if (pattern.empty()) {
          ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Nothing to play until the next SetPattern / stop
          continue;
        }

        for (const State& state : pattern) {
          apply(state);

          // Sleep for the state's duration, but wake immediately on a new pattern or a stop request
          if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(state.duration)) != 0) {
            break;
          }
        }
      }

      TaskUtils::TaskExiting(m_taskExited);
    }

    SimpleMutex m_patternMutex;
    std::vector<State> m_pattern;  // Guarded by m_patternMutex
    uint32_t m_generation = 0;     // Guarded by m_patternMutex; bumped on every SetPattern

    TaskHandle_t m_taskHandle = nullptr;
    TaskUtils::TaskExitFlag m_taskExited {false};
    std::atomic<bool> m_stopRequested {false};
  };
}  // namespace OpenShock
