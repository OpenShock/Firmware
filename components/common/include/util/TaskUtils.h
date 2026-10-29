#pragma once

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

  // Raw task watchdog calls on the calling task; use TaskWatchdog, which can't leave a task subscribed.
  /// @brief Returns whether this call added the subscription (false if the task already was subscribed, or on error).
  bool WatchdogSubscribe();
  void WatchdogFeed();
  void WatchdogUnsubscribe();

  /// @brief Keeps the calling task on the task watchdog while subscribed, and takes it off when destroyed, so no exit
  ///        path leaves it subscribed. While subscribed the task must call feed() more often than
  ///        CONFIG_ESP_TASK_WDT_TIMEOUT_S, or the watchdog reports it as stuck: unsubscribe() before blocking for
  ///        longer than that. Belongs to the task that created it; never share it with another task.
  class TaskWatchdog {
  public:
    /// @param subscribed Whether to subscribe right away.
    explicit TaskWatchdog(bool subscribed = true)
    {
      if (subscribed) {
        subscribe();
      }
    }
    ~TaskWatchdog() { unsubscribe(); }

    TaskWatchdog(const TaskWatchdog&)            = delete;
    TaskWatchdog& operator=(const TaskWatchdog&) = delete;

    void subscribe()
    {
      if (!m_subscribed) {
        m_subscribed = WatchdogSubscribe();  // Not ours to remove if something else on this task already added it
      }
    }
    void unsubscribe()
    {
      if (m_subscribed) {
        WatchdogUnsubscribe();
        m_subscribed = false;
      }
    }
    /// @brief Tells the watchdog this task is alive. Does nothing while unsubscribed.
    void feed() const
    {
      if (m_subscribed) {
        WatchdogFeed();
      }
    }

  private:
    bool m_subscribed = false;
  };
}  // namespace OpenShock::TaskUtils
