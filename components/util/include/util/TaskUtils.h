#pragma once

#include <freertos/task.h>

#include <cstdint>

namespace OpenShock::TaskUtils {
  /// @brief Create a task on the specified core, or the default core if the specified core is invalid
  BaseType_t TaskCreateUniversal(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask, const BaseType_t xCoreID);

  /// @brief Create a task on the core that does expensive work, this should not run on the core that handles WiFi
  BaseType_t TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask);

  /// @brief Waits for a task to self-delete within the given timeout. Force-kills it if it doesn't exit in time.
  ///        The caller is responsible for signaling the task to stop before calling this.
  void StopTask(TaskHandle_t taskHandle, const char* tag, const char* taskName, TickType_t timeout = pdMS_TO_TICKS(1000));
}  // namespace OpenShock::TaskUtils
