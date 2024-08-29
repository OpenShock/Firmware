#pragma once

#include <freertos/task.h>

#include <cstdint>

namespace OpenShock::TaskUtils {
  /// @brief Create a task on the specified core, or the default core if the specified core is invalid
  esp_err_t TaskCreateUniversal(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask, const BaseType_t xCoreID);

  /// @brief Create a task on the core that does expensive work, this should not run on the core that handles WiFi
  esp_err_t TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask);
}  // namespace OpenShock::TaskUtils
