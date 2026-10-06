// Fakes for everything device_control's CommandHandler/RFTransmitter touch outside
// the component. The ESP-IDF APIs (FreeRTOS queues, tasks and semaphores, RMT,
// esp_event, esp_timer) are ESP-IDF's own CMock mocks with the callbacks below
// installed; config, TaskUtils and EStopManager are plain fake definitions.
#include "host_fakes.h"

#include "unity.h"

#include "estop/EStopManager.h"
#include "util/TaskUtils.h"

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

// CMock generates C headers without C++ linkage guards.
extern "C" {
#include "Mockesp_event.h"
#include "Mockesp_timer.h"
#include "Mockqueue.h"
#include "Mockrmt_common.h"
#include "Mockrmt_encoder.h"
#include "Mockrmt_tx.h"
#include "Mocktask.h"
}

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <string>

namespace {
  // Behind a QueueHandle_t: either a queue or (xSemaphoreCreateMutex) a mutex.
  struct FakeQueue {
    bool isMutex;
    size_t length;
    size_t itemSize;
    std::deque<std::vector<uint8_t>> items;
    bool held;  // Mutex only
  };

  // Behind a TaskHandle_t.
  struct FakeTask {
    TaskFunction_t fn;
    void* arg;
    std::string name;
    bool stopped;
  };

  FakeQueue* AsQueue(QueueHandle_t handle)
  {
    return reinterpret_cast<FakeQueue*>(handle);
  }
  QueueHandle_t AsHandle(FakeQueue* queue)
  {
    return reinterpret_cast<QueueHandle_t>(queue);
  }
  FakeTask* AsTask(TaskHandle_t handle)
  {
    return reinterpret_cast<FakeTask*>(handle);
  }

  int64_t TicksToMs(TickType_t ticks)
  {
    return static_cast<int64_t>(ticks) * portTICK_PERIOD_MS;
  }
}  // namespace

namespace HostFake {
  int64_t Now         = 1'000'000;
  bool EStopped       = false;
  int64_t RmtFrameMs  = 10;
  bool FailRmtChannel = false;
  std::vector<Transmission> Transmissions;
  std::function<void()> OnIdle;
  std::function<void()> OnTransmit;
  OpenShock::Config::RFConfig RfConfig    = {.txPin = static_cast<gpio_num_t>(4), .keepAliveEnabled = false};
  OpenShock::Config::EStopConfig EStopCfg = {.enabled = false, .gpioPin = GPIO_NUM_NC};
  esp_event_handler_t EStopHandler        = nullptr;

  static std::vector<FakeQueue*> s_liveQueues;  // Queues only; mutexes are not counted
  static std::vector<std::unique_ptr<FakeTask>> s_tasks;
  static FakeTask* s_runningTask = nullptr;     // Set while RunTask() runs a task body
  static int s_idleBudget        = 0;           // Only consulted while s_runningTask is set

  void Reset()
  {
    EStopped       = false;
    RmtFrameMs     = 10;
    FailRmtChannel = false;
    Transmissions.clear();
    OnIdle     = nullptr;
    OnTransmit = nullptr;
  }

  size_t QueueLength(QueueHandle_t queue)
  {
    return AsQueue(queue)->items.size();
  }

  size_t LiveQueueCount()
  {
    return s_liveQueues.size();
  }

  QueueHandle_t NewestQueue()
  {
    return s_liveQueues.empty() ? nullptr : AsHandle(s_liveQueues.back());
  }

  TaskHandle_t FindTask(const char* namePrefix)
  {
    for (auto it = s_tasks.rbegin(); it != s_tasks.rend(); ++it) {
      if (!(*it)->stopped && (*it)->name.rfind(namePrefix, 0) == 0) {
        return reinterpret_cast<TaskHandle_t>(it->get());
      }
    }

    return nullptr;
  }

  bool TaskStopped(TaskHandle_t task)
  {
    return AsTask(task)->stopped;
  }

  bool RunTask(TaskHandle_t task, int idleBudget)
  {
    FakeTask* fakeTask = AsTask(task);

    s_runningTask = fakeTask;
    s_idleBudget  = idleBudget;
    bool returned = false;
    try {
      fakeTask->fn(fakeTask->arg);
      returned = true;
    }
    catch (const TaskBlocked&) {
    }
    s_runningTask = nullptr;
    s_idleBudget  = 0;

    return returned;
  }
}  // namespace HostFake

using namespace HostFake;

// --- FreeRTOS queues and mutexes (tools/mocks/freertos: Mockqueue.h) ---

static QueueHandle_t fakeQueueGenericCreate(const UBaseType_t length, const UBaseType_t itemSize, const uint8_t, int)
{
  auto* queue = new FakeQueue {.isMutex = false, .length = length, .itemSize = itemSize, .items = {}, .held = false};
  s_liveQueues.push_back(queue);
  return AsHandle(queue);
}

static QueueHandle_t fakeQueueCreateMutex(const uint8_t, int)
{
  return AsHandle(new FakeQueue {.isMutex = true, .length = 1, .itemSize = 0, .items = {}, .held = false});
}

// xQueueSend, and xSemaphoreGive on a mutex.
static BaseType_t fakeQueueGenericSend(QueueHandle_t handle, const void* const item, TickType_t, const BaseType_t, int)
{
  FakeQueue* queue = AsQueue(handle);
  if (queue->isMutex) {
    if (!queue->held) {
      return pdFALSE;  // Giving a mutex nobody holds
    }
    queue->held = false;
    return pdTRUE;
  }

  if (queue->items.size() >= queue->length) {
    return pdFALSE;
  }

  const auto* bytes = static_cast<const uint8_t*>(item);
  queue->items.emplace_back(bytes, bytes + queue->itemSize);
  return pdTRUE;
}

// xSemaphoreTake. Everything runs on the test thread, so a held mutex can only be
// released by whoever is blocked on it: waiting would deadlock on the device.
static BaseType_t fakeQueueSemaphoreTake(QueueHandle_t handle, TickType_t ticksToWait, int)
{
  FakeQueue* mutex = AsQueue(handle);
  if (!mutex->held) {
    mutex->held = true;
    return pdTRUE;
  }

  if (ticksToWait == portMAX_DELAY) {
    TEST_FAIL_MESSAGE("xSemaphoreTake(portMAX_DELAY) on a mutex that is already held would deadlock");
  }

  Now += TicksToMs(ticksToWait);
  return pdFALSE;
}

static BaseType_t fakeQueueReceive(QueueHandle_t handle, void* const buffer, TickType_t ticksToWait, int)
{
  FakeQueue* queue = AsQueue(handle);

  if (queue->items.empty()) {
    if (s_runningTask == nullptr) {
      // The test thread itself (e.g. RFTransmitter::destroy() draining its queue).
      if (ticksToWait == portMAX_DELAY) {
        TEST_FAIL_MESSAGE("xQueueReceive(portMAX_DELAY) on an empty queue outside a task would block forever");
      }
      Now += TicksToMs(ticksToWait);
      return pdFALSE;
    }

    if (s_idleBudget <= 0) {
      throw TaskBlocked {};
    }
    s_idleBudget--;

    if (OnIdle) {
      OnIdle();
    }

    if (queue->items.empty()) {
      if (ticksToWait == portMAX_DELAY) {
        throw TaskBlocked {};
      }

      // A zero-timeout poll still takes a moment; without this a polling loop would never see time pass.
      Now += ticksToWait == 0 ? 1 : TicksToMs(ticksToWait);
      return pdFALSE;
    }
  }

  std::memcpy(buffer, queue->items.front().data(), queue->itemSize);
  queue->items.pop_front();
  return pdTRUE;
}

static void fakeQueueDelete(QueueHandle_t handle, int)
{
  FakeQueue* queue = AsQueue(handle);
  s_liveQueues.erase(std::remove(s_liveQueues.begin(), s_liveQueues.end(), queue), s_liveQueues.end());
  delete queue;
}

// --- FreeRTOS tasks (Mocktask.h). Task creation goes through the TaskUtils fake below. ---

static void fakeTaskDelay(const TickType_t ticks, int)
{
  Now += TicksToMs(ticks);
}

// --- esp_timer (tools/mocks/esp_timer), behind Temporal.h's micros()/millis() ---

static int64_t fakeTimerGetTime(int)
{
  return Now * 1000;
}

// --- RMT (tools/mocks/driver) ---

struct rmt_channel_t {
  gpio_num_t pin;
};

static esp_err_t fakeRmtNewTxChannel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* ret_chan, int)
{
  if (FailRmtChannel) {
    return ESP_FAIL;
  }

  *ret_chan = new rmt_channel_t {.pin = config->gpio_num};
  return ESP_OK;
}

static esp_err_t fakeRmtEnableDisable(rmt_channel_handle_t, int)
{
  return ESP_OK;
}

static esp_err_t fakeRmtDelChannel(rmt_channel_handle_t channel, int)
{
  delete channel;
  return ESP_OK;
}

static esp_err_t fakeRmtNewCopyEncoder(const rmt_copy_encoder_config_t*, rmt_encoder_handle_t* ret_encoder, int)
{
  *ret_encoder = new rmt_encoder_t {};
  return ESP_OK;
}

static esp_err_t fakeRmtDelEncoder(rmt_encoder_handle_t encoder, int)
{
  delete encoder;
  return ESP_OK;
}

static esp_err_t fakeRmtTransmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void* payload, size_t payload_bytes, const rmt_transmit_config_t*, int)
{
  const auto* symbols = static_cast<const rmt_symbol_word_t*>(payload);

  Transmission tx {.at = Now, .symbols = {}};
  for (size_t i = 0; i < payload_bytes / sizeof(rmt_symbol_word_t); i++) {
    tx.symbols.push_back(symbols[i].val);
  }
  Transmissions.push_back(std::move(tx));

  if (OnTransmit) {
    OnTransmit();
  }

  return ESP_OK;
}

static esp_err_t fakeRmtTxWaitAllDone(rmt_channel_handle_t, int, int)
{
  Now += RmtFrameMs;
  return ESP_OK;
}

// --- esp_event (tools/mocks/esp_event) ---

static esp_err_t fakeEventHandlerRegister(esp_event_base_t, int32_t event_id, esp_event_handler_t event_handler, void*, int)
{
  if (event_id == OPENSHOCK_EVENT_ESTOP_STATE_CHANGED) {
    EStopHandler = event_handler;
  }
  return ESP_OK;
}

// Installed before any C++ static constructor runs: CommandHandler.cpp's static
// SimpleMutexes call xSemaphoreCreateMutex during static initialization.
__attribute__((constructor(101))) static void installMockCallbacks()
{
  xQueueGenericCreate_Stub(fakeQueueGenericCreate);
  xQueueCreateMutex_Stub(fakeQueueCreateMutex);
  xQueueGenericSend_Stub(fakeQueueGenericSend);
  xQueueSemaphoreTake_Stub(fakeQueueSemaphoreTake);
  xQueueReceive_Stub(fakeQueueReceive);
  vQueueDelete_Stub(fakeQueueDelete);
  vTaskDelay_Stub(fakeTaskDelay);

  esp_timer_get_time_Stub(fakeTimerGetTime);

  rmt_new_tx_channel_Stub(fakeRmtNewTxChannel);
  rmt_enable_Stub(fakeRmtEnableDisable);
  rmt_disable_Stub(fakeRmtEnableDisable);
  rmt_del_channel_Stub(fakeRmtDelChannel);
  rmt_new_copy_encoder_Stub(fakeRmtNewCopyEncoder);
  rmt_del_encoder_Stub(fakeRmtDelEncoder);
  rmt_transmit_Stub(fakeRmtTransmit);
  rmt_tx_wait_all_done_Stub(fakeRmtTxWaitAllDone);

  esp_event_handler_register_Stub(fakeEventHandlerRegister);
}

// --- TaskUtils (common's util/TaskUtils.h): record tasks instead of starting them ---

BaseType_t OpenShock::TaskUtils::TaskCreateUniversal(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t, void* const pvParameters, UBaseType_t, TaskHandle_t* const pvCreatedTask, const BaseType_t)
{
  s_tasks.push_back(std::make_unique<FakeTask>(FakeTask {.fn = pvTaskCode, .arg = pvParameters, .name = pcName, .stopped = false}));
  if (pvCreatedTask != nullptr) {
    *pvCreatedTask = reinterpret_cast<TaskHandle_t>(s_tasks.back().get());
  }
  return pdPASS;
}

BaseType_t OpenShock::TaskUtils::TaskCreateExpensive(TaskFunction_t pvTaskCode, const char* const pcName, const uint32_t usStackDepth, void* const pvParameters, UBaseType_t uxPriority, TaskHandle_t* const pvCreatedTask)
{
  return TaskCreateUniversal(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, 1);
}

void OpenShock::TaskUtils::TaskExiting(TaskExitFlag& exited)
{
  exited.store(true, std::memory_order_relaxed);
}

void OpenShock::TaskUtils::StopTask(TaskHandle_t taskHandle, TaskExitFlag& exited, const char*, const char*, TickType_t)
{
  if (taskHandle != nullptr) {
    AsTask(taskHandle)->stopped = true;
  }
  exited.store(true, std::memory_order_relaxed);
}

// --- Config ---

bool OpenShock::Config::GetRFConfig(RFConfig& out)
{
  out = RfConfig;
  return true;
}

bool OpenShock::Config::GetRFConfigTxPin(gpio_num_t& out)
{
  out = RfConfig.txPin;
  return true;
}

bool OpenShock::Config::SetRFConfigTxPin(gpio_num_t txPin)
{
  RfConfig.txPin = txPin;
  return true;
}

bool OpenShock::Config::GetRFConfigKeepAliveEnabled(bool& out)
{
  out = RfConfig.keepAliveEnabled;
  return true;
}

bool OpenShock::Config::SetRFConfigKeepAliveEnabled(bool enabled)
{
  RfConfig.keepAliveEnabled = enabled;
  return true;
}

bool OpenShock::Config::GetEStop(EStopConfig& out)
{
  out = EStopCfg;
  return true;
}

bool OpenShock::Config::GetEStopGpioPin(gpio_num_t& out)
{
  out = EStopCfg.gpioPin;
  return true;
}

// --- EStopManager (the real one samples GPIO from its own task; see EStopStateMachine for its logic) ---

bool OpenShock::EStopManager::Init()
{
  return true;
}

bool OpenShock::EStopManager::SetEStopEnabled(bool)
{
  return true;
}

bool OpenShock::EStopManager::SetEStopPin(gpio_num_t)
{
  return true;
}

bool OpenShock::EStopManager::IsEStopped()
{
  return EStopped;
}

int64_t OpenShock::EStopManager::LastEStopped()
{
  return EStopped ? Now : 0;
}

void OpenShock::EStopManager::SoftwareTrigger()
{
  EStopped = true;
}
