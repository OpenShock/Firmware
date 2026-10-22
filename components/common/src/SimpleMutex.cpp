#include <freertos/FreeRTOS.h>

#include "SimpleMutex.h"

const char* const TAG = "SimpleMutex";

OpenShock::SimpleMutex::SimpleMutex()
  : m_mutex(xSemaphoreCreateMutex())
{
}

OpenShock::SimpleMutex::~SimpleMutex()
{
  vSemaphoreDelete(m_mutex);
}

bool OpenShock::SimpleMutex::lock(TickType_t xTicksToWait)
{
  return xSemaphoreTake(m_mutex, xTicksToWait) == pdTRUE;
}

void OpenShock::SimpleMutex::unlock()
{
  xSemaphoreGive(m_mutex);
}
