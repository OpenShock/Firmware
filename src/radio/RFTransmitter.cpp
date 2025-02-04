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
const int64_t SEQUENCE_TIME_TO_LIVE          = 1000;

using namespace OpenShock;

struct command_t {
  int64_t until;
  ShockerModelType model;
  ShockerCommandType type;
  uint16_t shockerId;
  uint8_t intensity;
  bool overwrite : 1;
  bool destroy   : 1;
};
struct sequence_t {
  int64_t until;
  Rmt::MainEncoder encoder;
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

  m_queueHandle = xQueueCreate(RFTRANSMITTER_QUEUE_SIZE, sizeof(command_t));
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

  command_t cmd = command_t {.until = OpenShock::millis() + durationMs, .model = model, .type = type, .shockerId = shockerId, .intensity = intensity, .overwrite = overwriteExisting, .destroy = false};

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

  command_t command;
  while (xQueueReceive(m_queueHandle, &command, 0) == pdPASS) {
  }
}

void RFTransmitter::destroy()
{
  if (m_taskHandle != nullptr) {
    OS_LOGD(TAG, "[pin-%hhi] Stopping task", m_txPin);

    // Wait for the task to stop
    command_t cmd {.destroy = true};
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

  std::vector<sequence_t> sequences;
  while (true) {
    // Receive commands
    command_t cmd;
    while (xQueueReceive(m_queueHandle, &cmd, sequences.empty() ? portMAX_DELAY : 0) == pdTRUE) {
      // Destroy task if we receive destroy command
      if (cmd.destroy) {
        sequences.clear();
        vTaskDelete(nullptr);
        return;
      }

      if (cmd.overwrite) {
        // Replace the sequence if it already exists
        auto it = std::find_if(sequences.begin(), sequences.end(), [&cmd](const sequence_t& seq) { return seq.encoder.shockerId() == cmd.shockerId; });
        if (it != sequences.end()) {
          it->encoder.fillSequence(cmd.type, cmd.intensity);
          it->until = cmd.until;
          continue;
        }
      }

      Rmt::MainEncoder encoder(cmd.model, cmd.shockerId);
      if (encoder.is_valid()) {
        encoder.fillSequence(cmd.type, cmd.intensity);

        // If the command was not replaced, add it to the queue
        sequences.push_back(sequence_t {.until = cmd.until, .encoder = std::move(encoder)});
      }
    }

    if (OpenShock::EStopManager::IsEStopped()) {
      int64_t whenEStoppedTime = EStopManager::LastEStopped();

      for (auto seq = sequences.begin(); seq != sequences.end(); ++seq) {
        seq->until = whenEStoppedTime;
      }
    }

    // Send queued commands
    for (auto seq = sequences.begin(); seq != sequences.end();) {
      bool expired = seq->until < OpenShock::millis();

      // Remove expired commands, else send the command.
      // After sending/receiving a command, move to the next one.
      if (expired) {
        // Send the zero sequence to stop the shocker
        rmtWriteBlocking(m_rmtHandle, seq->encoder.terminator(), seq->encoder.size());

        if (seq->until + TRANSMIT_END_DURATION < OpenShock::millis()) {
          // Remove the command and move to the next one
          seq = sequences.erase(seq);
        } else {
          // Move to the next command
          ++seq;
        }
      } else {
        // Send the command
        rmtWriteBlocking(m_rmtHandle, seq->encoder.payload(), seq->encoder.size());

        // Move to the next command
        ++seq;
      }
    }
  }
}
