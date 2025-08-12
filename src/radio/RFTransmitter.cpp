#include <freertos/FreeRTOS.h>

#include "radio/RFTransmitter.h"

const char* const TAG = "RFTransmitter";

#include "Core.h"
#include "Logging.h"
#include "radio/rmt/ShockerSequence.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <freertos/queue.h>

namespace Const {
  const UBaseType_t kQueueSize         = 64;
  const BaseType_t kTaskPriority       = 1;
  const uint32_t kTaskStackSize        = 4096;  // PROFILED: 1.4KB stack usage
  const float kTickrateNs              = 1000;
  const int64_t kTransmitEndDurationMs = 300;
  const uint8_t kFlagOverwrite         = 1 << 0;
  const UBaseType_t kNotifKillFlag     = 1 << 0;
  const UBaseType_t kNotifHaltFlag     = 1 << 1;
  const UBaseType_t kNotifContinueFlag = 1 << 2;
  const UBaseType_t kTaskIdleDelay     = pdMS_TO_TICKS(5);
}  // namespace Const

using namespace OpenShock;

struct RFTransmitter::Command {
  int64_t lifetimeEnd;
  ShockerModelType modelType;
  ShockerCommandType type;
  uint16_t shockerId;
  uint8_t intensity;
  uint8_t flags;
};

RFTransmitter::RFTransmitter(gpio_num_t gpioPin)
  : m_txPin(gpioPin)
  , m_rmtHandle(nullptr)
  , m_queueHandle(nullptr)
  , m_taskHandle(nullptr)
{
  OS_LOGD(TAG, "[pin-%hhi] Creating RFTransmitter", m_txPin);

  m_rmtHandle = rmtInit(static_cast<int>(m_txPin), RMT_TX_MODE, RMT_MEM_64);
  if (m_rmtHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create rmt object", m_txPin);
    destroy();
    return;
  }

  float realTick = rmtSetTick(m_rmtHandle, Const::kTickrateNs);
  OS_LOGD(TAG, "[pin-%hhi] real tick set to: %fns", m_txPin, realTick);

  m_queueHandle = xQueueCreate(Const::kQueueSize, sizeof(Command));
  if (m_queueHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create queue", m_txPin);
    destroy();
    return;
  }

  char name[32];
  snprintf(name, sizeof(name), "RFTransmitter-%hhi", m_txPin);

  if (TaskUtils::TaskCreateExpensive(&Util::FnProxy<&RFTransmitter::TransmitTask>, name, Const::kTaskStackSize, this, Const::kTaskPriority, &m_taskHandle) != pdPASS) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create task", m_txPin);
    destroy();
    return;
  }
}

RFTransmitter::~RFTransmitter()
{
  destroy();
}

bool RFTransmitter::SendCommand(ShockerModelType model, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint16_t durationMs, bool overwriteExisting)
{
  if (m_queueHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Queue is null", m_txPin);
    return false;
  }

  Command cmd = Command {.lifetimeEnd = OpenShock::millis() + durationMs, .modelType = model, .type = type, .shockerId = shockerId, .intensity = intensity, .flags = overwriteExisting ? Const::kFlagOverwrite : (uint8_t)0u};

  // Add the command to the queue, wait max 10 ms (Adjust this)
  if (xQueueSend(m_queueHandle, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to send command to queue", m_txPin);
    return false;
  }

  return true;
}

void RFTransmitter::ClearPendingCommands()
{
  if (m_queueHandle == nullptr) {
    return;
  }

  OS_LOGI(TAG, "[pin-%hhi] Clearing pending commands", m_txPin);

  Command command;
  while (xQueueReceive(m_queueHandle, &command, 0) == pdPASS) {
    // discard command
  }
}

static void SendTaskNotif(TaskHandle_t handle, uint32_t flags)
{
  xTaskNotifyIndexed(handle, 0, flags, eSetBits);
}
static bool TryGetTaskNotif(uint32_t& flags, TickType_t ticksToWait)
{
  return xTaskGenericNotifyWait(0, 0, ULONG_MAX, &flags, ticksToWait) == pdPASS;
}

bool RFTransmitter::Halt()
{
  ClearPendingCommands();

  if (m_taskHandle == nullptr) return false;

  SendTaskNotif(m_taskHandle, Const::kNotifHaltFlag);

  return true;
}

bool RFTransmitter::Continue()
{
  if (m_taskHandle == nullptr) return false;

  SendTaskNotif(m_taskHandle, Const::kNotifContinueFlag);

  return true;
}

void RFTransmitter::destroy()
{
  if (m_taskHandle != nullptr) {
    OS_LOGD(TAG, "[pin-%hhi] Stopping task", m_txPin);

    // Signal task to stop
    SendTaskNotif(m_taskHandle, Const::kNotifKillFlag);

    // Wait for the task to stop
    uint16_t msWaited = 0;
    while (eTaskGetState(m_taskHandle) != eDeleted && msWaited < 250) {
      vTaskDelay(pdMS_TO_TICKS(10));
      msWaited += 10;
    }
    if (msWaited < 250) {
      OS_LOGD(TAG, "[pin-%hhi] Task stopped", m_txPin);
    } else {
      OS_LOGD(TAG, "[pin-%hhi] Task failed to stop, forcing.", m_txPin);
      vTaskDelete(m_taskHandle);
    }

    // Clear the queue
    ClearPendingCommands();

    m_taskHandle = nullptr;
  }
  if (m_queueHandle != nullptr) {
    vQueueDelete(m_queueHandle);
    m_queueHandle = nullptr;
  }
  if (m_rmtHandle != nullptr) {
    rmtDeinit(m_rmtHandle);
    m_rmtHandle = nullptr;
  }
}

static bool addSequence(std::vector<Rmt::ShockerSequence>& sequences, ShockerModelType modelType, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity, int64_t lifetimeEnd)
{
  Rmt::ShockerSequence sequence(modelType, shockerId, lifetimeEnd);
  if (!sequence.is_valid()) return false;

  if (!sequence.fill(commandType, intensity)) return false;

  sequences.push_back(std::move(sequence));

  return true;
}

static bool modifySequence(std::vector<Rmt::ShockerSequence>& sequences, ShockerModelType modelType, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity, int64_t lifetimeEnd)
{
  for (auto& seq : sequences) {
    if (seq.shockerModel() == modelType && seq.shockerId() == shockerId) {
      bool ok = seq.fill(commandType, intensity);
      seq.setLifetimeEnd(ok ? lifetimeEnd : 0);  // Remove this immidiatley if fill didnt succeed
      return ok;                                 // Will generate a new sequence if fill failed
    }
  }

  return false;
}

static void writeSequences(rmt_obj_t* rmt_handle, std::vector<Rmt::ShockerSequence>& sequences)
{
  // Send queued commands
  for (auto seq = sequences.begin(); seq != sequences.end();) {
    int64_t timeToLive = seq->lifetimeEnd() - OpenShock::millis();

    if (timeToLive > 0) {
      // Send the command
      rmtWriteBlocking(rmt_handle, seq->payload(), seq->size());
    } else {
      // Remove command if it has sent out its termination sequence for long enough
      if (timeToLive + Const::kTransmitEndDurationMs <= 0) {
        seq = sequences.erase(seq);
        continue;
      }

      // Send the termination sequence to stop the shocker
      rmtWriteBlocking(rmt_handle, seq->terminator(), seq->size());
    }

    // Move to the next command
    ++seq;
  }
}

void RFTransmitter::TransmitTask()
{
  OS_LOGD(TAG, "[pin-%hhi] RMT loop running on core %d", m_txPin, xPortGetCoreID());

  std::vector<Rmt::ShockerSequence> sequences;
  while (true) {
    uint32_t data = 0;
    if (TryGetTaskNotif(data, 0)) {
      // Check if task is being killed
      if ((data & Const::kNotifKillFlag) != 0) break;

      // Check if task is being halted
      if ((data & Const::kNotifHaltFlag) != 0) {
        // Kill all sequences by expiring their lifetime and starting their termination transmissions
        int64_t now = OpenShock::millis();
        for (auto& seq : sequences) {
          seq.setLifetimeEnd(now);
        }

        // Write terminators
        while (!sequences.empty()) {
          writeSequences(m_rmtHandle, sequences);
        }

        // Constantly clear pending commands while waiting for continue or kill flag
        while (true) {
          data = 0;
          if (TryGetTaskNotif(data, Const::kTaskIdleDelay)) {
            // Check if task is being killed
            if ((data & Const::kNotifKillFlag) != 0) {
              vTaskDelete(nullptr);
            }

            if ((data & Const::kNotifContinueFlag) != 0) {
              break;
            }
          }

          ClearPendingCommands();
        }
        continue;
      }
    }

    // Receive commands
    Command cmd;
    while (xQueueReceive(m_queueHandle, &cmd, sequences.empty() ? Const::kTaskIdleDelay : 0) == pdTRUE) {
      if ((cmd.flags & Const::kFlagOverwrite) != 0) {
        // Replace the sequence if it already exists
        if (modifySequence(sequences, cmd.modelType, cmd.shockerId, cmd.type, cmd.intensity, cmd.lifetimeEnd)) {
          continue;
        }
      }

      if (!addSequence(sequences, cmd.modelType, cmd.shockerId, cmd.type, cmd.intensity, cmd.lifetimeEnd)) {
        OS_LOGD(TAG, "[pin-%hhi] Failed to add sequence", m_txPin);
      }
    }

    writeSequences(m_rmtHandle, sequences);
  }

  vTaskDelete(nullptr);  // kill current task
}
