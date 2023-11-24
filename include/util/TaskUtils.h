#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace OpenShock::TaskUtils {
  /// @brief Create a task on the core that does expensive work, this should not run on the core that handles WiFi
  /// @param pvTaskCode
  /// @param pcName
  /// @param usStackDepth
  /// @param pvParameters
  /// @param uxPriority
  /// @param pvCreatedTask
  /// @return
  inline esp_err_t TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void * const pvParameters, UBaseType_t uxPriority, TaskHandle_t * const pvCreatedTask) {
#if portNUM_PROCESSORS == 2
    // Run on core 1 (0 handles WiFi and should be minimally used)
    return xTaskCreatePinnedToCore(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, 1);
#else
#if portNUM_PROCESSORS > 2
#warning "This chip has more than 2 cores. Please update this code to use the correct core."
#endif
    // Run on any core
    return xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask);
#endif
  }
} // namespace OpenShock
