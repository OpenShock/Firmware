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
  // freed memory; the flag is the only thing it can safely read.
  exited.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

// A task that still hasn't exited after this long is wedged
static const TickType_t kStopTaskHardLimit = pdMS_TO_TICKS(10'000);

void TaskUtils::StopTask(TaskHandle_t /*taskHandle*/, TaskExitFlag& exited, const char* tag, const char* taskName, TickType_t timeout)
{
  // Never vTaskDelete() the task from here: it could be holding a mutex, a heap block or a peripheral, and FreeRTOS
  // releases none of them. A slow task gets more time; one that never exits is a fault.
  const TickType_t tickInterval = pdMS_TO_TICKS(10);
  TickType_t elapsed            = 0;
  TickType_t nextWarning        = timeout;
  while (!exited.load(std::memory_order_acquire)) {
    if (elapsed >= kStopTaskHardLimit) {
      OS_PANIC(tag, "%s did not exit within %lu ms", taskName, static_cast<unsigned long>(pdTICKS_TO_MS(elapsed)));
    }
    if (elapsed >= nextWarning) {
      OS_LOGE(tag, "%s has not exited after %lu ms, still waiting", taskName, static_cast<unsigned long>(pdTICKS_TO_MS(elapsed)));
      nextWarning += timeout;
    }
    vTaskDelay(tickInterval);
    elapsed += tickInterval;
  }
}
