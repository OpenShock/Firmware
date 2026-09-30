#pragma once

#include <freertos/task.h>

#include <atomic>
#include <cstdint>

namespace OpenShock::TaskUtils {
  /// @brief A task's exit signal, owned by whoever owns the task.
  ///        The task publishes it through TaskExiting() immediately before deleting
  ///        itself, and StopTask() waits on it. It exists because a self-deleted
  ///        task's handle cannot be inspected: vTaskDelete() hands the TCB to the
  ///        idle task, which frees it, so eTaskGetState() on that handle is a
  ///        use-after-free once the idle task has run.
  using TaskExitFlag = std::atomic<bool>;

  /// @brief Create a task on the specified core, or the default core if the specified core is invalid
  BaseType_t TaskCreateUniversal(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask, const BaseType_t xCoreID);

  /// @brief Create a task on the core that does expensive work, this should not run on the core that handles WiFi
  BaseType_t TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask);

  /// @brief Ends the calling task. Use this at the end of a task function instead of
  ///        vTaskDelete(nullptr), so StopTask() can tell that the task is gone without
  ///        touching its handle. Does not return.
  void TaskExiting(TaskExitFlag& exited);

  /// @brief Waits for a task to exit within the given timeout. Force-kills it if it doesn't exit in time.
  ///        The caller is responsible for signaling the task to stop before calling this.
  ///        `exited` must be the same flag the task passes to TaskExiting(), and must be
  ///        cleared before the task is created.
  void StopTask(TaskHandle_t taskHandle, TaskExitFlag& exited, const char* tag, const char* taskName, TickType_t timeout = pdMS_TO_TICKS(1000));
}  // namespace OpenShock::TaskUtils
