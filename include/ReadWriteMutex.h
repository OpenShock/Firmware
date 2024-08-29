#pragma once

#include <freertos/semphr.h>

namespace OpenShock {
  class ReadWriteMutex {
  public:
    ReadWriteMutex();
    ~ReadWriteMutex();

    bool lockRead(TickType_t xTicksToWait);
    void unlockRead();

    bool lockWrite(TickType_t xTicksToWait);
    void unlockWrite();
  private:
    SemaphoreHandle_t m_mutex;
    SemaphoreHandle_t m_readSem;
    int m_readers;
  };

  class ScopedReadLock {
  public:
    ScopedReadLock(ReadWriteMutex* mutex, TickType_t xTicksToWait = portMAX_DELAY) : m_mutex(mutex) {
      bool result = false;
      if (m_mutex != nullptr) {
        result = m_mutex->lockRead(xTicksToWait);
      }

      if (!result) {
        m_mutex = nullptr;
      }
    }

    ~ScopedReadLock() {
      if (m_mutex != nullptr) {
        m_mutex->unlockRead();
      }
    }

    bool isLocked() const {
      return m_mutex != nullptr;
    }

    bool unlock() {
      if (m_mutex != nullptr) {
        m_mutex->unlockRead();
        m_mutex = nullptr;
        return true;
      }

      return false;
    }

    ReadWriteMutex* getMutex() const {
      return m_mutex;
    }
  private:
    ReadWriteMutex* m_mutex;
  };

  class ScopedWriteLock {
  public:
    ScopedWriteLock(ReadWriteMutex* mutex, TickType_t xTicksToWait = portMAX_DELAY) : m_mutex(mutex) {
      bool result = false;
      if (m_mutex != nullptr) {
        result = m_mutex->lockWrite(xTicksToWait);
      }

      if (!result) {
        m_mutex = nullptr;
      }
    }

    ~ScopedWriteLock() {
      if (m_mutex != nullptr) {
        m_mutex->unlockWrite();
      }
    }

    bool isLocked() const {
      return m_mutex != nullptr;
    }

    bool unlock() {
      if (m_mutex != nullptr) {
        m_mutex->unlockWrite();
        m_mutex = nullptr;
        return true;
      }

      return false;
    }

    ReadWriteMutex* getMutex() const {
      return m_mutex;
    }
  private:
    ReadWriteMutex* m_mutex;
  };
} // namespace OpenShock
