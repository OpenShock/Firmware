#pragma once

#include <freertos/semphr.h>

#include "Common.h"

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
