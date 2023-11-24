#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace OpenShock::TaskUtils {
  /// @brief Create a task on the specified core, or the default core if the specified core is invalid
  inline esp_err_t TaskCreateUniversal(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void * const pvParameters, UBaseType_t uxPriority, TaskHandle_t * const pvCreatedTask, const BaseType_t xCoreID) {
#ifndef CONFIG_FREERTOS_UNICORE
    if (xCoreID >= 0 && xCoreID < portNUM_PROCESSORS) {
      return xTaskCreatePinnedToCore(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, xCoreID);
    }
#endif
    return xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask);
  }

  /// @brief Create a task on the core that does expensive work, this should not run on the core that handles WiFi
  inline esp_err_t TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void * const pvParameters, UBaseType_t uxPriority, TaskHandle_t * const pvCreatedTask) {
#if portNUM_PROCESSORS > 2
#warning "This chip has more than 2 cores. Please update this code to use the correct core."
#endif
    // Run on core 1 (0 handles WiFi and should be minimally used)
    return TaskCreateUniversal(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, 1);
  }
} // namespace OpenShock
