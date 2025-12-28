#include <freertos/FreeRTOS.h>

#include "radio/RFTransmitter.h"

const char* const TAG = "RFTransmitter";

#include "Core.h"
#include "estop/EStopManager.h"
#include "Logging.h"
#include "radio/rmt/Sequence.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <freertos/queue.h>

#include <vector>

const UBaseType_t kQueueSize        = 64;
const BaseType_t kTaskPriority      = 1;
const uint32_t kTaskStackSize       = 4096;  // PROFILED: 1.4KB stack usage
const float kTickrateNs             = 1000;
const int64_t kTerminatorDurationMs = 300;
const uint8_t kFlagOverwrite        = 1 << 0;
const uint8_t kFlagDeleteTask       = 1 << 1;
const TickType_t kTaskIdleDelay     = pdMS_TO_TICKS(5);

using namespace OpenShock;

struct RFTransmitter::Command {
  int64_t transmitEnd;
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

  float realTick = rmtSetTick(m_rmtHandle, kTickrateNs);
  OS_LOGD(TAG, "[pin-%hhi] real tick set to: %fns", m_txPin, realTick);

  m_queueHandle = xQueueCreate(kQueueSize, sizeof(Command));
  if (m_queueHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create queue", m_txPin);
    destroy();
    return;
  }

  char name[32];
  snprintf(name, sizeof(name), "RFTransmitter-%u", m_txPin);

  if (TaskUtils::TaskCreateExpensive(&Util::FnProxy<&RFTransmitter::TransmitTask>, name, kTaskStackSize, this, kTaskPriority, &m_taskHandle) != pdPASS) {
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

  // Stop logic
  if (type == ShockerCommandType::Stop) {
    OS_LOGV(TAG, "Stop command received");

    type              = ShockerCommandType::Vibrate;
    intensity         = 0;
    durationMs        = 300;
    overwriteExisting = true;
  } else {
    OS_LOGD(TAG, "Command received: %u %u %u %u", model, shockerId, type, intensity);
  }

  Command cmd = Command {.transmitEnd = OpenShock::millis() + durationMs, .modelType = model, .type = type, .shockerId = shockerId, .intensity = intensity, .flags = overwriteExisting ? kFlagOverwrite : (uint8_t)0};

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
  }
}

void RFTransmitter::destroy()
{
  if (m_taskHandle != nullptr) {
    OS_LOGD(TAG, "[pin-%hhi] Stopping task", m_txPin);

    // Wait for the task to stop
    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.flags = kFlagDeleteTask;

    while (eTaskGetState(m_taskHandle) != eDeleted) {
      vTaskDelay(pdMS_TO_TICKS(10));

      // Send nullptr to stop the task gracefully
      xQueueSend(m_queueHandle, &cmd, pdMS_TO_TICKS(10));
    }

    OS_LOGD(TAG, "[pin-%hhi] Task stopped", m_txPin);

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

static bool addSequence(std::vector<Rmt::Sequence>& sequences, ShockerModelType modelType, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity, int64_t transmitEnd)
{
  Rmt::Sequence sequence(modelType, shockerId, transmitEnd);
  if (!sequence.is_valid()) return false;

  if (!sequence.fill(commandType, intensity)) return false;

  sequences.push_back(std::move(sequence));

  return true;
}

static bool modifySequence(std::vector<Rmt::Sequence>& sequences, ShockerModelType modelType, uint16_t shockerId, ShockerCommandType commandType, uint8_t intensity, int64_t transmitEnd)
{
  for (auto& seq : sequences) {
    if (seq.shockerModel() == modelType && seq.shockerId() == shockerId) {
      bool ok = seq.fill(commandType, intensity);
      seq.setTransmitEnd(ok ? transmitEnd : 0);  // Remove this immediately if fill didnt succeed
      return ok;                                 // Returns whether modification succeeded; caller should generate a new sequence if this fails
    }
  }

  return false;
}

static void writeSequences(rmt_obj_t* rmt_handle, std::vector<Rmt::Sequence>& sequences)
{
  // Send queued commands
  for (auto seq = sequences.begin(); seq != sequences.end();) {
    int64_t timeToLive = seq->transmitEnd() - OpenShock::millis();

    if (timeToLive > 0) {
      // Send the command
      rmtWriteBlocking(rmt_handle, seq->payload(), seq->size());
    } else {
      // Remove command if it has sent out its termination sequence for long enough
      if (timeToLive <= -kTerminatorDurationMs) {
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

  bool wasEstopped = false;
  std::vector<Rmt::Sequence> sequences;
  while (true) {
    // Receive commands
    Command cmd;
    while (xQueueReceive(m_queueHandle, &cmd, sequences.empty() ? portMAX_DELAY : 0) == pdTRUE) {
      // Destroy task if we receive destroy command
      if ((cmd.flags & kFlagDeleteTask) != 0) {
        sequences.clear();
        vTaskDelete(nullptr);
        return;
      }

      if ((cmd.flags & kFlagOverwrite) != 0) {
        // Replace the sequence if it already exists
        if (modifySequence(sequences, cmd.modelType, cmd.shockerId, cmd.type, cmd.intensity, cmd.transmitEnd)) {
          continue;
        }
      }

      if (!addSequence(sequences, cmd.modelType, cmd.shockerId, cmd.type, cmd.intensity, cmd.transmitEnd)) {
        OS_LOGD(TAG, "[pin-%hhi] Failed to add sequence");
      }
    }

    bool isEstopped = OpenShock::EStopManager::IsEStopped();
    if (isEstopped != wasEstopped) {
      wasEstopped = isEstopped;

      if (isEstopped) {
        // Set all sequences to transmit their terminators
        int64_t now = OpenShock::millis();
        for (auto seq = sequences.begin(); seq != sequences.end(); ++seq) {
          seq->setTransmitEnd(now);
        }
      }
    }

    writeSequences(m_rmtHandle, sequences);
  }
}
