#include "RFTransmitter.h"
#include "EStopManager.h"

#include "Logging.h"
#include "Rmt/MainEncoder.h"
#include "Time.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <limits>

struct command_t {
  std::int64_t until;
  std::vector<rmt_data_t> sequence;
  std::shared_ptr<std::vector<rmt_data_t>> zeroSequence;
  std::uint16_t shockerId;
};

using namespace OpenShock;

RFTransmitter::RFTransmitter(std::uint8_t gpioPin, int queueSize) : m_txPin(gpioPin), m_rmtHandle(nullptr), m_queueHandle(nullptr), m_taskHandle(nullptr) {
  snprintf(m_name, sizeof(m_name), "RFTransmitter-%u", gpioPin);

  ESP_LOGD(m_name, "Creating RFTransmitter");

  m_rmtHandle = rmtInit(gpioPin, RMT_TX_MODE, RMT_MEM_64);
  if (m_rmtHandle == nullptr) {
    ESP_LOGE(m_name, "Failed to create rmt object");
    destroy();
    return;
  }

  float realTick = rmtSetTick(m_rmtHandle, 1000);
  ESP_LOGD(m_name, "real tick set to: %fns", realTick);

  m_queueHandle = xQueueCreate(queueSize, sizeof(command_t*));
  if (m_queueHandle == nullptr) {
    ESP_LOGE(m_name, "Failed to create queue");
    destroy();
    return;
  }

  if (xTaskCreate(TransmitTask, m_name, 4096, this, 1, &m_taskHandle) != pdPASS) {
    ESP_LOGE(m_name, "Failed to create task");
    destroy();
    return;
  }
}

RFTransmitter::~RFTransmitter() {
  destroy();
}

bool RFTransmitter::SendCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs) {
  if (m_queueHandle == nullptr) {
    ESP_LOGE(m_name, "Queue is null");
    return false;
  }

  // Intensity must be between 0 and 99
  intensity = std::min(intensity, (std::uint8_t)99);

  command_t* cmd = new command_t {.until = OpenShock::millis() + durationMs, .sequence = Rmt::GetSequence(model, shockerId, type, intensity), .zeroSequence = Rmt::GetZeroSequence(model, shockerId), .shockerId = shockerId};

  // We will use nullptr commands to end the task, if we got a nullptr here, we are out of memory... :(
  if (cmd == nullptr) {
    ESP_LOGE(m_name, "Failed to allocate command");
    return false;
  }

  // Add the command to the queue, wait max 10 ms (Adjust this)
  if (xQueueSend(m_queueHandle, &cmd, 10 / portTICK_PERIOD_MS) != pdTRUE) {
    ESP_LOGE(m_name, "Failed to send command to queue");
    delete cmd;
    return false;
  }

  return true;
}

void RFTransmitter::ClearPendingCommands() {
  if (m_queueHandle == nullptr) {
    return;
  }

  ESP_LOGV(m_name, "Clearing pending commands");

  command_t* command;
  while (xQueueReceive(m_queueHandle, &command, 0) == pdPASS) {
    delete command;
  }
}

void RFTransmitter::destroy() {
  if (m_taskHandle != nullptr) {
    ESP_LOGD(m_name, "Stopping task");

    // Wait for the task to stop
    command_t* cmd = nullptr;
    while (eTaskGetState(m_taskHandle) != eDeleted) {
      vTaskDelay(10 / portTICK_PERIOD_MS);

      // Send nullptr to stop the task gracefully
      xQueueSend(m_queueHandle, &cmd, 10 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(m_name, "Task stopped");

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

void RFTransmitter::TransmitTask(void* arg) {
  RFTransmitter* transmitter = reinterpret_cast<RFTransmitter*>(arg);
  const char* name           = transmitter->m_name;
  rmt_obj_t* rmtHandle       = transmitter->m_rmtHandle;
  QueueHandle_t queueHandle  = transmitter->m_queueHandle;

  ESP_LOGD(name, "RMT loop running on core %d", xPortGetCoreID());

  std::vector<command_t*> commands;
  while (true) {
    // Receive commands
    command_t* cmd = nullptr;
    while (xQueueReceive(queueHandle, &cmd, 0) == pdTRUE) {
      if (cmd == nullptr) {
        ESP_LOGD(name, "Received nullptr (stop command), cleaning up...");

        for (auto it = commands.begin(); it != commands.end(); ++it) {
          delete *it;
        }

        ESP_LOGD(name, "Cleanup done, stopping task");

        vTaskDelete(nullptr);
        return;
      }

      // Replace the command if it already exists
      bool replaced = false;
      for (auto it = commands.begin(); it != commands.end(); ++it) {
        if ((*it)->shockerId == cmd->shockerId) {
          delete *it;
          *it = cmd;

          replaced = true;

          break;
        }
      }

      // If the command was not replaced, add it to the queue
      if (!replaced) {
        commands.push_back(cmd);
      }
    }

    // Send queued commands
    for (auto it = commands.begin(); it != commands.end();) {
      cmd = *it;

      bool expired = cmd->until < OpenShock::millis();
      bool empty   = cmd->sequence.size() <= 0;

      // Remove expired or empty commands, else send the command.
      // After sending/receiving a command, move to the next one.
      if (expired || empty || OpenShock::EStopManager::IsEStopped()) {
        // If the command is not empty, send the zero sequence to stop the shocker
        if (!empty) {
          rmtWriteBlocking(rmtHandle, cmd->zeroSequence->data(), cmd->zeroSequence->size());
        }

        // Remove the command and move to the next one
        it = commands.erase(it);
        delete cmd;
      } else {
        // Send the command
        rmtWriteBlocking(rmtHandle, cmd->sequence.data(), cmd->sequence.size());

        // Move to the next command
        ++it;
      }
    }
  }
}
