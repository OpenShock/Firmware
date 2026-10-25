#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "Diagnostics.h"
#include "serial/Serial.h"

#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <algorithm>
#include <memory>
#include <new>

static void printCounters()
{
  using namespace OpenShock::Diagnostics;
  for (std::size_t i = 0; i < static_cast<std::size_t>(Counter::_Count); i++) {
    auto counter = static_cast<Counter>(i);
    auto name    = Name(counter);
    SERPR_RESPONSE("Diag|Counter|%.*s|%lu", static_cast<int>(name.size()), name.data(), static_cast<unsigned long>(Get(counter)));
  }
  SERPR_RESPONSE("Diag|Counter|SerialWritesDropped|%lu", static_cast<unsigned long>(OpenShock::Serial::DroppedWrites()));
}

static void printHeap()
{
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  SERPR_RESPONSE("Diag|Heap|Free|%u", static_cast<unsigned>(heap_caps_get_free_size(caps)));
  SERPR_RESPONSE("Diag|Heap|MinFree|%u", static_cast<unsigned>(heap_caps_get_minimum_free_size(caps)));
  SERPR_RESPONSE("Diag|Heap|LargestBlock|%u", static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
}

// Every task's stack high-water mark: the least free stack it has had, in bytes. Lowest first.
static void printTasks()
{
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
  // Headroom for tasks created between the count and the snapshot; uxTaskGetSystemState returns 0 if it is too small
  UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4;
  std::unique_ptr<TaskStatus_t[]> tasks(new (std::nothrow) TaskStatus_t[capacity]);
  if (tasks == nullptr) {
    SERPR_ERROR("Diag|Tasks|Out of memory");
    return;
  }

  UBaseType_t count = uxTaskGetSystemState(tasks.get(), capacity, nullptr);
  if (count == 0) {
    SERPR_ERROR("Diag|Tasks|Task list changed, try again");
    return;
  }

  std::sort(tasks.get(), tasks.get() + count, [](const TaskStatus_t& a, const TaskStatus_t& b) { return a.usStackHighWaterMark < b.usStackHighWaterMark; });
  for (UBaseType_t i = 0; i < count; i++) {
    const TaskStatus_t& task = tasks[i];
    SERPR_RESPONSE("Diag|Task|%s|StackMinFree|%lu|Priority|%u", task.pcTaskName, static_cast<unsigned long>(task.usStackHighWaterMark), static_cast<unsigned>(task.uxCurrentPriority));
  }
#else
  SERPR_RESPONSE("Diag|Tasks|Unavailable (CONFIG_FREERTOS_USE_TRACE_FACILITY is off)");
#endif
}

static void handleDiagCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;
  (void)isAutomated;

  printCounters();
  printHeap();
  printTasks();

  SERPR_SUCCESS("Diag|Done");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::DiagHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("diag"sv);

  group.addCommand("Show drop counters, heap minimums and every task's stack high-water mark"sv, handleDiagCommand);

  return group;
}
