#include "radio/RFTransmitter.h"
#include "EStopManager.h"

#include "Logging.h"
#include "Time.h"
#include "util/TaskUtils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <limits>

const char* const TAG = "RFTransmitter";

using namespace OpenShock;

RFTransmitter::RFTransmitter(std::uint8_t gpioPin, int queueSize) : m_txPin(gpioPin), m_rmtHandle(nullptr), m_transmitQueueHandle(nullptr), m_transmitTaskHandle(nullptr), m_keepAliveQueueHandle(nullptr), m_keepAliveTaskHandle(nullptr) {
  ESP_LOGD(TAG, "[pin-%u] Creating RFTransmitter", m_txPin);

  m_rmtHandle = rmtInit(gpioPin, RMT_TX_MODE, RMT_MEM_64);
  if (m_rmtHandle == nullptr) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create rmt object", m_txPin);
    destroy();
    return;
  }

  float realTick = rmtSetTick(m_rmtHandle, 1000);
  ESP_LOGD(TAG, "[pin-%u] real tick set to: %fns", m_txPin, realTick);

  m_transmitQueueHandle = xQueueCreate(queueSize, sizeof(command_t*));
  if (m_transmitQueueHandle == nullptr) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create queue", m_txPin);
    destroy();
    return;
  }

#if OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS > 0
  m_keepAliveQueueHandle = xQueueCreate(queueSize, sizeof(command_t*));
  if (m_keepAliveQueueHandle == nullptr) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create keep-alive queue", m_txPin);
    destroy();
    return;
  }
#endif

  char name[32];
  snprintf(name, sizeof(name), "RFTransmitter-%u", m_txPin);

  if (TaskUtils::TaskCreateExpensive(TransmitTask, name, 4096, this, 1, &m_transmitTaskHandle) != pdPASS) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create task", m_txPin);
    destroy();
    return;
  }

#if OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS > 0
  snprintf(name, sizeof(name), "RFTranKA-%u", m_txPin);

  if (xTaskCreate(KeepAliveTask, name, 4096, this, 1, &m_keepAliveTaskHandle) != pdPASS) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create shocker keep-alive task", m_txPin);
    destroy();
    return;
  }
#endif
}

RFTransmitter::~RFTransmitter() {
  destroy();
}

bool RFTransmitter::SendCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs) {
  if (m_transmitQueueHandle == nullptr) {
    ESP_LOGE(TAG, "[pin-%u] Queue is null", m_txPin);
    return false;
  }

  // Intensity must be between 0 and 99
  intensity = std::min(intensity, (std::uint8_t)99);

  command_t* cmd = new command_t {.timestamp = OpenShock::millis() + durationMs, .sequence = Rmt::GetSequence(model, shockerId, type, intensity), .zeroSequence = Rmt::GetZeroSequence(model, shockerId), .shockerId = shockerId, .isKeepAlive = false};

  // We will use nullptr commands to end the task, if we got a nullptr here, we are out of memory... :(
  if (cmd == nullptr) {
    ESP_LOGE(TAG, "[pin-%u] Failed to allocate command", m_txPin);
    return false;
  }

  // Add the command to the queue, wait max 10 ms (Adjust this)
  if (xQueueSend(m_transmitQueueHandle, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
    ESP_LOGE(TAG, "[pin-%u] Failed to send command to queue", m_txPin);
    delete cmd;
    return false;
  }

  return true;
}

void RFTransmitter::ClearPendingCommands() {
  if (m_transmitQueueHandle == nullptr) {
    return;
  }

  ESP_LOGI(TAG, "[pin-%u] Clearing pending commands", m_txPin);

  command_t* command;
  while (xQueueReceive(m_transmitQueueHandle, &command, 0) == pdPASS) {
    delete command;
  }
}

void RFTransmitter::destroy() {
  if (m_transmitTaskHandle != nullptr) {
    ESP_LOGD(TAG, "[pin-%u] Stopping task", m_txPin);

    // Wait for the task to stop
    command_t* cmd = nullptr;
    while (eTaskGetState(m_transmitTaskHandle) != eDeleted) {
      vTaskDelay(pdMS_TO_TICKS(10));

      // Send nullptr to stop the task gracefully
      xQueueSend(m_transmitQueueHandle, &cmd, pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "[pin-%u] Task stopped", m_txPin);

    // Clear the queue
    ClearPendingCommands();

    m_transmitTaskHandle = nullptr;
  }
  if (m_transmitQueueHandle != nullptr) {
    vQueueDelete(m_transmitQueueHandle);
    m_transmitQueueHandle = nullptr;
  }
  if (m_rmtHandle != nullptr) {
    rmtDeinit(m_rmtHandle);
    m_rmtHandle = nullptr;
  }
#if OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS > 0
  if (m_keepAliveTaskHandle != nullptr) {
    ESP_LOGD(TAG, "[pin-%u] Stopping keep-alive task", m_txPin);

    // Wait for the task to stop
    command_t* cmd = nullptr;
    while (eTaskGetState(m_keepAliveTaskHandle) != eDeleted) {
      vTaskDelay(pdMS_TO_TICKS(10));

      // Send nullptr to stop the task gracefully
      xQueueSend(m_keepAliveQueueHandle, &cmd, pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "[pin-%u] Keep-alive task stopped", m_txPin);

    m_keepAliveTaskHandle = nullptr;
  }
  if (m_keepAliveQueueHandle != nullptr) {
    vQueueDelete(m_keepAliveQueueHandle);
    m_keepAliveQueueHandle = nullptr;
  }
#endif
}

void RFTransmitter::replaceOrAddCommand(std::vector<command_t*>& commands, command_t* newCmd, bool checkKeepAlive = false, bool createCopy = false) {
  bool replaced = false;
  for (auto it = commands.begin(); it != commands.end(); ++it) {
    if ((*it)->shockerId == newCmd->shockerId) {
      // If we get sent a keep-alive command, and we already have something in the queue, ignore it.
      if (checkKeepAlive && newCmd->isKeepAlive) {
        delete newCmd;
        return;
      }
      // Otherwise, replace the command
      delete *it;
      if (createCopy) {
        *it = new command_t {.timestamp = newCmd->timestamp, .sequence = newCmd->sequence, .zeroSequence = newCmd->zeroSequence, .shockerId = newCmd->shockerId, .isKeepAlive = newCmd->isKeepAlive};
      } else {
        *it = newCmd;
      }
      replaced = true;
      break;
    }
  }

  if (!replaced) {
    if (createCopy) {
      commands.push_back(new command_t {.timestamp = newCmd->timestamp, .sequence = newCmd->sequence, .zeroSequence = newCmd->zeroSequence, .shockerId = newCmd->shockerId, .isKeepAlive = newCmd->isKeepAlive});
    } else {
      commands.push_back(newCmd);
    }
  }
}

#if OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS > 0
void RFTransmitter::KeepAliveTask(void* arg) {
  RFTransmitter* transmitter         = reinterpret_cast<RFTransmitter*>(arg);
  QueueHandle_t transmitQueueHandle  = transmitter->m_transmitQueueHandle;
  QueueHandle_t keepAliveQueueHandle = transmitter->m_keepAliveQueueHandle;

  ESP_LOGD(TAG, "Keep-Alive loop running on core %d", xPortGetCoreID());

  std::vector<command_t*> keepAliveCommands;
  while (true) {
    // Receive commands from CommandHandler via queue
    command_t* cmd = nullptr;
    while (xQueueReceive(keepAliveQueueHandle, &cmd, 0) == pdTRUE) {
      if (cmd == nullptr) {
        ESP_LOGD(TAG, "Keep-Alive task received nullptr (stop command), cleaning up...");

        for (auto it = keepAliveCommands.begin(); it != keepAliveCommands.end(); ++it) {
          delete *it;
        }

        ESP_LOGD(TAG, "Keep-Alive task cleanup done, stopping task");

        vTaskDelete(nullptr);
        return;
      }

      RFTransmitter::replaceOrAddCommand(keepAliveCommands, cmd);
    }

    // Add keep-alive commands to commands queue if the timestamp has passed
    for (auto it = keepAliveCommands.begin(); it != keepAliveCommands.end();) {
      cmd = *it;

      bool sendKeepAlive = cmd->timestamp < OpenShock::millis();

      if (sendKeepAlive) {
        // ESP_LOGD(TAG, "Sending shocker keep-alive command, free heap: %u", xPortGetFreeHeapSize());
        cmd->timestamp   = OpenShock::millis() + 1000;
        cmd->isKeepAlive = true;
        command_t* copy  = new command_t {.timestamp = cmd->timestamp, .sequence = cmd->sequence, .zeroSequence = cmd->zeroSequence, .shockerId = cmd->shockerId, .isKeepAlive = true};
        if (xQueueSend(transmitQueueHandle, &copy, 0) != pdTRUE) {
          // ESP_LOGE(TAG, "Failed to send keep-alive command to queue");
          delete copy;
          ++it;
        } else {
          it = keepAliveCommands.erase(it);
          delete cmd;
        }
      } else {
        ++it;
      }
    }
    vTaskDelay(1);
  }
}
#endif

// Task for transmitting RF to Shockers, as well as handling Shocker Keepalive
// This task will run on APP_CORE when available
// Keep-Alive is handled by adding a copy of the command to the keep-alive queue whenever a "stop" is supposed to be sent.
// And after OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS passes, we send the zero sequence to keep the shocker awake.
// The keep-alive queue is cleared by Emergency Stops
void RFTransmitter::TransmitTask(void* arg) {
  RFTransmitter* transmitter         = reinterpret_cast<RFTransmitter*>(arg);
  std::uint8_t m_txPin               = transmitter->m_txPin;  // This must be defined here, because the THIS_LOG macro uses it
  rmt_obj_t* rmtHandle               = transmitter->m_rmtHandle;
  QueueHandle_t transmitQueueHandle  = transmitter->m_transmitQueueHandle;
  QueueHandle_t keepAliveQueueHandle = transmitter->m_keepAliveQueueHandle;

  ESP_LOGD(TAG, "[pin-%u] RMT loop running on core %d", m_txPin, xPortGetCoreID());

  std::vector<command_t*> commands;
  while (true) {
    // Receive commands from CommandHandler via queue
    command_t* cmd = nullptr;
    while (xQueueReceive(transmitQueueHandle, &cmd, 0) == pdTRUE) {
      if (cmd == nullptr) {
        ESP_LOGD(TAG, "[pin-%u] Received nullptr (stop command), cleaning up...", m_txPin);

        for (auto it = commands.begin(); it != commands.end(); ++it) {
          delete *it;
        }

        ESP_LOGD(TAG, "[pin-%u] Cleanup done, stopping task", m_txPin);

        vTaskDelete(nullptr);
        return;
      }

      RFTransmitter::replaceOrAddCommand(commands, cmd, true);
    }

    // Send queued commands
    for (auto it = commands.begin(); it != commands.end();) {
      cmd = *it;

      bool expired = cmd->timestamp < OpenShock::millis();
      bool empty   = cmd->sequence.size() <= 0;

      // Remove expired or empty commands, else send the command.
      // After sending/receiving a command, move to the next one.
      if (expired || empty || OpenShock::EStopManager::IsEStopped()) {
        // If the command is not empty, send the zero sequence to stop the shocker
        if (!empty && cmd->zeroSequence->size() > 0) {
          rmtWriteBlocking(rmtHandle, cmd->zeroSequence->data(), cmd->zeroSequence->size());
#if OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS > 0
          // Add the command to the keep-alive queue
          if (!OpenShock::EStopManager::IsEStopped()) {
            // When to send the keep-alive message
            cmd->timestamp = OpenShock::millis() + OPENSHOCK_SHOCKER_KEEPALIVE_INTERVAL_MS;
            // What to send
            cmd->sequence   = *cmd->zeroSequence;
            command_t* copy = new command_t {.timestamp = cmd->timestamp, .sequence = cmd->sequence, .zeroSequence = cmd->zeroSequence, .shockerId = cmd->shockerId, .isKeepAlive = true};
            if (xQueueSend(keepAliveQueueHandle, &copy, 0) != pdTRUE) {
              // ESP_LOGE(TAG, "Failed to send keep-alive command to queue");
              delete copy;
            }
          }
#endif
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
