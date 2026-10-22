#pragma once

// Control surface for the fakes behind ESP-IDF's CMock mocks (implemented in
// host_fakes.cpp, which installs them as the mocks' callbacks before any static
// constructor runs).
//
// Tasks never run on their own: TaskCreate* only records them, and RunTask() calls
// the task body synchronously on the test thread. Inside RunTask(), a fake queue
// receive that would block forever (portMAX_DELAY on an empty queue), or that exceeds
// the run's idle budget, throws TaskBlocked to unwind the task loop back into
// RunTask(). Outside RunTask() (the test thread itself, e.g. a destructor draining a
// queue) an empty receive just times out; one that would block forever fails the test.
// A timed receive on an empty queue advances the fake clock by its timeout (inside a
// task, a zero-timeout poll by 1 ms), and every RMT frame advances it by RmtFrameMs, so
// task loops replay deterministically.
#include "config/Config.h"
#include "events/Events.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace HostFake {
  struct TaskBlocked { };

  struct Transmission {
    int64_t at;                     // Fake clock when the frame was handed to RMT
    std::vector<uint32_t> symbols;  // rmt_symbol_word_t::val of every symbol in the frame
  };

  extern int64_t Now;                       // OpenShock::millis() (esp_timer_get_time() / 1000)
  extern bool EStopped;                     // EStopManager::IsEStopped()
  extern int64_t RmtFrameMs;                // Fake clock advance per transmitted frame
  extern bool FailRmtChannel;               // Make rmt_new_tx_channel fail (RFTransmitter::ok() == false)
  extern std::vector<Transmission> Transmissions;
  extern std::function<void()> OnIdle;      // Called on every empty queue receive inside RunTask()
  extern std::function<void()> OnTransmit;  // Called after every recorded RMT frame
  extern OpenShock::Config::RFConfig RfConfig;
  extern OpenShock::Config::EStopConfig EStopCfg;
  extern esp_event_handler_t EStopHandler;  // Handler registered for OPENSHOCK_EVENT_ESTOP_STATE_CHANGED

  /// @brief Clears recorded frames and hooks and releases the e-stop. Queues and tasks are owned by the code under test.
  void Reset();

  size_t QueueLength(QueueHandle_t queue);
  size_t LiveQueueCount();
  QueueHandle_t NewestQueue();

  /// @brief Newest task whose name starts with namePrefix and has not been stopped, or nullptr.
  TaskHandle_t FindTask(const char* namePrefix);
  bool TaskStopped(TaskHandle_t task);

  /// @brief Runs the task body until it returns (true) or would block (false). idleBudget bounds how many empty queue receives it may make.
  bool RunTask(TaskHandle_t task, int idleBudget);
}  // namespace HostFake
