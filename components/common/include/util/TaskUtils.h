#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// For a task that is started and stopped, prefer util/ManagedTask.h, which wraps TaskExiting() and StopTask().
namespace OpenShock::TaskUtils {
  /// @brief The task-notification index OpenShock's own wakes use (ManagedTask, the WiFiManager task). Index 0 is the
  ///        one ESP-IDF drivers and the plain xTaskNotify* calls use, so a wake there could end a driver's wait early,
  ///        or a driver could consume ours. Falls back to 0 if the config has only one entry.
#if configTASK_NOTIFICATION_ARRAY_ENTRIES > 1
  inline constexpr UBaseType_t kNotifyIndex = 1;
#else
  inline constexpr UBaseType_t kNotifyIndex = 0;
#endif

  /// @brief Waits for a wake on kNotifyIndex (clearing it). Returns nonzero if woken, 0 on timeout.
  inline uint32_t WaitForNotify(TickType_t timeout)
  {
    return ulTaskNotifyTakeIndexed(kNotifyIndex, pdTRUE, timeout);
  }
  /// @brief Wakes `task` on kNotifyIndex.
  inline void Notify(TaskHandle_t task)
  {
    xTaskNotifyGiveIndexed(task, kNotifyIndex);
  }

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

  /// @brief Waits for a task to exit. Logs an error every `timeout` it keeps running, and restarts the device if it
  ///        is still running after a hard limit; it never deletes the task from outside.
  ///        The caller is responsible for signaling the task to stop before calling this.
  ///        `exited` must be the same flag the task passes to TaskExiting(), and must be
  ///        cleared before the task is created.
  void StopTask(TaskHandle_t taskHandle, TaskExitFlag& exited, const char* tag, const char* taskName, TickType_t timeout = pdMS_TO_TICKS(1000));

  /// @brief Puts the calling task on the task watchdog. From then on it must call WatchdogFeed() more often than
  ///        CONFIG_ESP_TASK_WDT_TIMEOUT_S, or the watchdog reports it as stuck. Only for loops that never wait longer
  ///        than that; call WatchdogUnsubscribe() before blocking indefinitely and before exiting.
  void WatchdogSubscribe();
  /// @brief Tells the task watchdog the calling task is alive. A no-op for a task that isn't subscribed.
  void WatchdogFeed();
  /// @brief Takes the calling task off the task watchdog. A no-op for a task that isn't subscribed.
  void WatchdogUnsubscribe();
}  // namespace OpenShock::TaskUtils
