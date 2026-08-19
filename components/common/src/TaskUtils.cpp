#include <freertos/FreeRTOS.h>

#include "util/TaskUtils.h"

#include "Logging.h"

using namespace OpenShock;

/// @brief Create a task on the specified core, or the default core if the specified core is invalid
BaseType_t TaskUtils::TaskCreateUniversal(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask, const BaseType_t xCoreID)
{
#ifndef CONFIG_FREERTOS_UNICORE
  if (xCoreID >= 0 && xCoreID < portNUM_PROCESSORS) {
    return xTaskCreatePinnedToCore(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, xCoreID);
  }
#endif
  return xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask);
}

/// @brief Create a task on the core that does expensive work, this should not run on the core that handles WiFi
BaseType_t TaskUtils::TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask)
{
#if portNUM_PROCESSORS > 2
#warning "This chip has more than 2 cores. Please update this code to use the correct core."
#endif
  // Run on core 1 (0 handles WiFi and should be minimally used)
  return TaskCreateUniversal(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, 1);
}

void TaskUtils::TaskExiting(TaskExitFlag& exited)
{
  // Publish before deleting. Once vTaskDelete() runs, the idle task is free to
  // reclaim this task's TCB, so the handle StopTask() holds may already point at
  // freed memory — the flag is the only thing it can safely read.
  exited.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

void TaskUtils::StopTask(TaskHandle_t taskHandle, TaskExitFlag& exited, const char* tag, const char* taskName, TickType_t timeout)
{
  const TickType_t tickInterval = pdMS_TO_TICKS(10);
  TickType_t elapsed            = 0;
  while (!exited.load(std::memory_order_acquire) && elapsed < timeout) {
    vTaskDelay(tickInterval);
    elapsed += tickInterval;
  }

  if (!exited.load(std::memory_order_acquire)) {
    // The task never reached TaskExiting(), so it has not deleted itself and the
    // handle is still valid to kill.
    OS_LOGW(tag, "%s did not exit gracefully, killing task", taskName);
    vTaskDelete(taskHandle);
  }
}
