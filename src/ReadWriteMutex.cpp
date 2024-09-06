#include <freertos/FreeRTOS.h>

#include "ReadWriteMutex.h"

const char* const TAG = "ReadWriteMutex";

#include "Logging.h"

OpenShock::ReadWriteMutex::ReadWriteMutex() : m_mutex(xSemaphoreCreateMutex()), m_readSem(xSemaphoreCreateBinary()), m_readers(0) {
  xSemaphoreGive(m_readSem);
}

OpenShock::ReadWriteMutex::~ReadWriteMutex() {
  vSemaphoreDelete(m_mutex);
  vSemaphoreDelete(m_readSem);
}

bool OpenShock::ReadWriteMutex::lockRead(TickType_t xTicksToWait) {
  if (xSemaphoreTake(m_readSem, xTicksToWait) == pdFALSE) {
    OS_LOGE(TAG, "Failed to take read semaphore");
    return false;
  }

  if (++m_readers == 1) {
    if (xSemaphoreTake(m_mutex, xTicksToWait) == pdFALSE) {
      xSemaphoreGive(m_readSem);
      return false;
    }
  }

  xSemaphoreGive(m_readSem);

  return true;
}

void OpenShock::ReadWriteMutex::unlockRead() {
  if (xSemaphoreTake(m_readSem, portMAX_DELAY) == pdFALSE) {
    OS_LOGE(TAG, "Failed to take read semaphore");
    return;
  }

  if (--m_readers == 0) {
    xSemaphoreGive(m_mutex);
  }

  xSemaphoreGive(m_readSem);
}

bool OpenShock::ReadWriteMutex::lockWrite(TickType_t xTicksToWait) {
  if (xSemaphoreTake(m_mutex, xTicksToWait) == pdFALSE) {
    OS_LOGE(TAG, "Failed to take mutex");
    return false;
  }

  return true;
}

void OpenShock::ReadWriteMutex::unlockWrite() {
  xSemaphoreGive(m_mutex);
}
