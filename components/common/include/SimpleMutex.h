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

  class ScopedLock {
    DISABLE_COPY(ScopedLock);
    DISABLE_MOVE(ScopedLock);

  public:
    ScopedLock(SimpleMutex* mutex, TickType_t xTicksToWait = portMAX_DELAY)
      : m_mutex(mutex)
    {
      bool result = false;
      if (m_mutex != nullptr) {
        result = m_mutex->lock(xTicksToWait);
      }

      if (!result) {
        m_mutex = nullptr;
      }
    }

    ~ScopedLock()
    {
      if (m_mutex != nullptr) {
        m_mutex->unlock();
      }
    }

    bool isLocked() const { return m_mutex != nullptr; }

    bool unlock()
    {
      if (m_mutex != nullptr) {
        m_mutex->unlock();
        m_mutex = nullptr;
        return true;
      }

      return false;
    }

    SimpleMutex* getMutex() const { return m_mutex; }

  private:
    SimpleMutex* m_mutex;
  };
}  // namespace OpenShock
