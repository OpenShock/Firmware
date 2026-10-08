#pragma once

#include "OpenShock.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace OpenShock {
  /// @brief Owns one FreeRTOS task: its handle, its stop request and its exit signal.
  ///
  /// The body is an ordinary function that returns when it is done. Returning destroys its locals before the task
  /// deletes itself, and the exit is published through TaskUtils::TaskExiting(), so the body never calls
  /// vTaskDelete() and never touches the exit flag. A long-running body polls stopRequested() at its wait points.
  ///
  /// stop() requests the stop, wakes the task and waits for it to exit (force-killing it after the timeout). The
  /// default wake is a task notification, which ends a ulTaskNotifyTake() wait; a task that blocks on something else
  /// (a queue) passes its own wake, e.g. one that posts a sentinel.
  ///
  /// start() and stop() must be serialized by the owner; notify(), running() and stopRequested() may be called from
  /// any task. Owners must call stop() in their own destructor before releasing anything the body uses; the destructor
  /// here is only a safety net.
  class ManagedTask {
    DISABLE_COPY(ManagedTask);
    DISABLE_MOVE(ManagedTask);

  public:
    ManagedTask(const char* tag, const char* description)
      : m_tag(tag)
      , m_description(description)
    {
    }
    ~ManagedTask() { stop(); }

    /// @brief Creates the task on `core` (-1 for any core). Returns true if it is already running.
    bool start(const char* name, uint32_t stackSize, UBaseType_t priority, BaseType_t core, std::function<void()> body)
    {
      ScopedLock lock__(&m_handleMutex);

      if (m_handle != nullptr) {
        return true;
      }

      m_body = std::move(body);
      m_stopRequested.store(false, std::memory_order_relaxed);
      m_handleReleased.store(false, std::memory_order_relaxed);
      m_exited.store(false, std::memory_order_relaxed);

      TaskHandle_t handle = nullptr;
      if (TaskUtils::TaskCreateUniversal(&ManagedTask::entry, name, stackSize, this, priority, &handle, core) != pdPASS) {
        m_body = nullptr;
        return false;
      }
      m_handle = handle;
      return true;
    }

    /// @brief Same as start(), on the core that does expensive work (see TaskUtils::TaskCreateExpensive).
    bool startExpensive(const char* name, uint32_t stackSize, UBaseType_t priority, std::function<void()> body) { return start(name, stackSize, priority, 1, std::move(body)); }

    /// @brief Requests a stop, wakes the task with a task notification and waits for it to exit.
    void stop(TickType_t timeout = pdMS_TO_TICKS(1000))
    {
      stop([this] { xTaskNotifyGive(m_handle); }, timeout);
    }

    /// @brief Requests a stop, wakes the task with `wake` and waits for it to exit.
    template<typename Wake>
    void stop(Wake&& wake, TickType_t timeout = pdMS_TO_TICKS(1000))
    {
      TaskHandle_t handle;
      {
        ScopedLock lock__(&m_handleMutex);

        handle = m_handle;
        if (handle == nullptr) {
          return;
        }

        m_stopRequested.store(true, std::memory_order_relaxed);
        wake();

        // From here on nothing touches the handle except a force-kill of a task that never exited, so the task may
        // now delete itself (see entry()). notify() checks m_stopRequested under the same lock, so it can't either.
        m_handleReleased.store(true, std::memory_order_release);
      }

      TaskUtils::StopTask(handle, m_exited, m_tag, m_description, timeout);

      ScopedLock lock__(&m_handleMutex);
      m_handle = nullptr;
      m_body   = nullptr;
    }

    /// @brief Wakes a task blocked in ulTaskNotifyTake() without stopping it. Safe from any task.
    void notify()
    {
      ScopedLock lock__(&m_handleMutex);
      if (m_handle != nullptr && !m_stopRequested.load(std::memory_order_relaxed)) {
        xTaskNotifyGive(m_handle);
      }
    }

    bool running() const
    {
      ScopedLock lock__(&m_handleMutex);
      return m_handle != nullptr;
    }

    /// @brief For the body: true once stop() has been called.
    bool stopRequested() const { return m_stopRequested.load(std::memory_order_relaxed); }

  private:
    static void entry(void* arg)
    {
      auto* self = static_cast<ManagedTask*>(arg);
      self->m_body();

      // A task that polls stopRequested() can see the request and get here while stop() is still about to wake it
      // through the handle. Deleting itself now would let the idle task free the TCB under that wake, so hold on
      // until stop() is done with the handle. A body that returns without a stop request waits here for stop() too.
      while (!self->m_handleReleased.load(std::memory_order_acquire)) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }

      TaskUtils::TaskExiting(self->m_exited);
    }

    const char* m_tag;
    const char* m_description;
    std::function<void()> m_body;
    mutable SimpleMutex m_handleMutex;  // Guards m_handle against notify()/running() on other tasks during start()/stop()
    TaskHandle_t m_handle = nullptr;
    TaskUtils::TaskExitFlag m_exited {false};
    std::atomic<bool> m_stopRequested {false};
    std::atomic<bool> m_handleReleased {false};  // Set by stop() once it no longer touches the handle
  };
}  // namespace OpenShock
