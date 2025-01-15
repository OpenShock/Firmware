#include <freertos/FreeRTOS.h>

#include "radio/RFTransmitter.h"

const char* const TAG = "RFTransmitter";

#include "estop/EStopManager.h"

#include "Logging.h"
#include "radio/rmt/MainEncoder.h"
#include "Time.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <freertos/queue.h>

const UBaseType_t RFTRANSMITTER_QUEUE_SIZE   = 64;
const BaseType_t RFTRANSMITTER_TASK_PRIORITY = 1;
const uint32_t RFTRANSMITTER_TASK_STACK_SIZE = 4096;  // PROFILED: 1.4KB stack usage
const float RFTRANSMITTER_TICKRATE_NS        = 1000;
const int64_t TRANSMIT_END_DURATION          = 300;

using namespace OpenShock;

struct command_t {
  int64_t until;
  std::vector<rmt_data_t> sequence;
  std::vector<rmt_data_t> zeroSequence;
  uint16_t shockerId;
  bool overwrite;
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

  float realTick = rmtSetTick(m_rmtHandle, RFTRANSMITTER_TICKRATE_NS);
  OS_LOGD(TAG, "[pin-%hhi] real tick set to: %fns", m_txPin, realTick);

  m_queueHandle = xQueueCreate(RFTRANSMITTER_QUEUE_SIZE, sizeof(command_t*));
  if (m_queueHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create queue", m_txPin);
    destroy();
    return;
  }

  char name[32];
  snprintf(name, sizeof(name), "RFTransmitter-%u", m_txPin);

  if (TaskUtils::TaskCreateExpensive(&Util::FnProxy<&RFTransmitter::TransmitTask>, name, RFTRANSMITTER_TASK_STACK_SIZE, this, RFTRANSMITTER_TASK_PRIORITY, &m_taskHandle) != pdPASS) {
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

  command_t* cmd = new command_t {.until = OpenShock::millis() + durationMs, .sequence = Rmt::GetSequence(model, shockerId, type, intensity), .zeroSequence = Rmt::GetZeroSequence(model, shockerId), .shockerId = shockerId, .overwrite = overwriteExisting};

  // We will use nullptr commands to end the task, if we got a nullptr here, we are out of memory... :(
  if (cmd == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to allocate command", m_txPin);
    return false;
  }

  // Add the command to the queue, wait max 10 ms (Adjust this)
  if (xQueueSend(m_queueHandle, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to send command to queue", m_txPin);
    delete cmd;
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

  command_t* command;
  while (xQueueReceive(m_queueHandle, &command, 0) == pdPASS) {
    delete command;
  }
}

void RFTransmitter::destroy()
{
  if (m_taskHandle != nullptr) {
    OS_LOGD(TAG, "[pin-%hhi] Stopping task", m_txPin);

    // Wait for the task to stop
    command_t* cmd = nullptr;
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

void RFTransmitter::TransmitTask()
{
  OS_LOGD(TAG, "[pin-%hhi] RMT loop running on core %d", m_txPin, xPortGetCoreID());

  std::vector<command_t*> commands;
  while (true) {
    // Receive commands
    command_t* cmd = nullptr;
    while (xQueueReceive(m_queueHandle, &cmd, commands.empty() ? portMAX_DELAY : 0) == pdTRUE) {
      if (cmd == nullptr) {
        OS_LOGD(TAG, "[pin-%hhi] Received nullptr (stop command), cleaning up...", m_txPin);

        for (auto it = commands.begin(); it != commands.end(); ++it) {
          delete *it;
        }

        OS_LOGD(TAG, "[pin-%hhi] Cleanup done, stopping task", m_txPin);

        vTaskDelete(nullptr);
        return;
      }

      // Replace the command if it already exists
      for (auto it = commands.begin(); it != commands.end(); ++it) {
        const command_t* existingCmd = *it;

        if (existingCmd->shockerId == cmd->shockerId) {
          // Only replace the command if it should be overwritten
          if (existingCmd->overwrite) {
            delete *it;
            *it = cmd;
          } else {
            delete cmd;
          }

          cmd = nullptr;

          break;
        }
      }

      // If the command was not replaced, add it to the queue
      if (cmd != nullptr) {
        commands.push_back(cmd);
      }
    }

    if (OpenShock::EStopManager::IsEStopped()) {
      int64_t whenEStoppedTime = EStopManager::LastEStopped();

      for (auto it = commands.begin(); it != commands.end(); ++it) {
        cmd = *it;

        cmd->until = whenEStoppedTime;
      }
    }

    // Send queued commands
    for (auto it = commands.begin(); it != commands.end();) {
      cmd = *it;

      bool expired = cmd->until < OpenShock::millis();
      bool empty   = cmd->sequence.empty();

      // Remove expired or empty commands, else send the command.
      // After sending/receiving a command, move to the next one.
      if (expired || empty) {
        // If the command is not empty, send the zero sequence to stop the shocker
        if (!empty) {
          rmtWriteBlocking(m_rmtHandle, cmd->zeroSequence.data(), cmd->zeroSequence.size());
        }

        if (cmd->until + TRANSMIT_END_DURATION < OpenShock::millis()) {
          // Remove the command and move to the next one
          it = commands.erase(it);
          delete cmd;
        } else {
          // Move to the next command
          ++it;
        }
      } else {
        // Send the command
        rmtWriteBlocking(m_rmtHandle, cmd->sequence.data(), cmd->sequence.size());

        // Move to the next command
        ++it;
      }
    }
  }
}
