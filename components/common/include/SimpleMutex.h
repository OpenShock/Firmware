#pragma once

#include <freertos/semphr.h>

#include "OpenShock.h"

namespace OpenShock {
  class SimpleMutex {
    DISABLE_COPY(SimpleMutex);
    DISABLE_MOVE(SimpleMutex);

  public:
    SimpleMutex();
    ~SimpleMutex();

    bool lock(TickType_t xTicksToWait);
    void unlock();

  private:
    // Statically allocated: creating it can't fail, which matters because most SimpleMutexes are constructed during
    // static initialization, where a failed allocation would go unnoticed and leave a null handle.
    StaticSemaphore_t m_storage;
    SemaphoreHandle_t m_mutex;
  };

  /// @brief Holds `mutex` from construction until destruction or unlock(). Waits as long as it takes, so it always
  ///        holds the lock; use ScopedTryLock to give up after a timeout.
  class ScopedLock {
    DISABLE_COPY(ScopedLock);
    DISABLE_MOVE(ScopedLock);

  public:
    explicit ScopedLock(SimpleMutex* mutex)
      : m_mutex(mutex)
    {
      m_mutex->lock(portMAX_DELAY);
    }

    ~ScopedLock() { unlock(); }

    /// @brief Releases the lock early; the destructor then does nothing.
    void unlock()
    {
      if (m_mutex != nullptr) {
        m_mutex->unlock();
        m_mutex = nullptr;
      }
    }

  private:
    SimpleMutex* m_mutex;
  };

  /// @brief Like ScopedLock, but gives up after `timeout`. Check isLocked() before touching what the mutex guards.
  class ScopedTryLock {
    DISABLE_COPY(ScopedTryLock);
    DISABLE_MOVE(ScopedTryLock);

  public:
    ScopedTryLock(SimpleMutex* mutex, TickType_t timeout)
      : m_mutex(mutex->lock(timeout) ? mutex : nullptr)
    {
    }

    ~ScopedTryLock()
    {
      if (m_mutex != nullptr) {
        m_mutex->unlock();
      }
    }

    [[nodiscard]] bool isLocked() const { return m_mutex != nullptr; }

  private:
    SimpleMutex* m_mutex;
  };
}  // namespace OpenShock
