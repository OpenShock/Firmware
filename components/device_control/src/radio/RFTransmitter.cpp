#include <freertos/FreeRTOS.h>

#include "radio/RFTransmitter.h"

const char* const TAG = "RFTransmitter";

#include "estop/EStopManager.h"
#include "Logging.h"

#include <cstring>
#include "radio/rmt/Sequence.h"
#include "Temporal.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <esp_err.h>
#include <soc/soc_caps.h>

#include <freertos/queue.h>

#include <vector>

const UBaseType_t kQueueSize        = 64;
const BaseType_t kTaskPriority      = 1;
const uint32_t kTaskStackSize       = 4096;  // PROFILED: 1.4KB stack usage
const float kTickrateNs             = 1000;
const int64_t kTerminatorDurationMs = 300;
const int32_t kRmtTimeoutMs         = 100;
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
  , m_queueHandle(nullptr)
  , m_taskHandle(nullptr)
  , m_rmtChannel(nullptr)
  , m_rmtEncoder(nullptr)
{
  OS_LOGD(TAG, "[pin-%hhi] Creating RFTransmitter", m_txPin);

  // 1 MHz resolution => 1 us tick (RF protocol timings are in microseconds).
  rmt_tx_channel_config_t channelConfig = {
    .gpio_num          = gpioPin,
    .clk_src           = RMT_CLK_SRC_DEFAULT,
    .resolution_hz     = 1'000'000,
    .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,  // exactly 1 memory block
    .trans_queue_depth = 4,
  };
  esp_err_t err = rmt_new_tx_channel(&channelConfig, &m_rmtChannel);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create RMT TX channel: %s", m_txPin, esp_err_to_name(err));
    destroy();
    return;
  }

  rmt_copy_encoder_config_t encoderConfig = {};
  err                                     = rmt_new_copy_encoder(&encoderConfig, &m_rmtEncoder);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create RMT copy encoder: %s", m_txPin, esp_err_to_name(err));
    destroy();
    return;
  }

  err = rmt_enable(m_rmtChannel);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to enable RMT channel: %s", m_txPin, esp_err_to_name(err));
    destroy();
    return;
  }

  m_queueHandle = xQueueCreate(kQueueSize, sizeof(Command));
  if (m_queueHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create queue", m_txPin);
    destroy();
    return;
  }

  char name[32];
  snprintf(name, sizeof(name), "RFTransmitter-%u", m_txPin);

  if (TaskUtils::TaskCreateExpensive(Util::FnProxy<&RFTransmitter::TransmitTask>, name, kTaskStackSize, this, kTaskPriority, &m_taskHandle) != pdPASS) {
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

    // Send kill command + wait for task to exit
    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.flags = kFlagDeleteTask;
    xQueueSend(m_queueHandle, &cmd, pdMS_TO_TICKS(10));

    TaskUtils::StopTask(m_taskHandle, TAG, "RFTransmitter task");

    OS_LOGD(TAG, "[pin-%hhi] Task stopped", m_txPin);

    // Clear the queue
    ClearPendingCommands();

    m_taskHandle = nullptr;
  }
  if (m_queueHandle != nullptr) {
    vQueueDelete(m_queueHandle);
    m_queueHandle = nullptr;
  }
  if (m_rmtChannel != nullptr) {
    rmt_disable(m_rmtChannel);
    rmt_del_channel(m_rmtChannel);
    m_rmtChannel = nullptr;
  }
  if (m_rmtEncoder != nullptr) {
    rmt_del_encoder(m_rmtEncoder);
    m_rmtEncoder = nullptr;
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

static void writeSequences(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, std::vector<Rmt::Sequence>& sequences)
{
  rmt_transmit_config_t txConfig = {};
  txConfig.flags.eot_level       = 0;

  // Send queued commands
  for (auto seq = sequences.begin(); seq != sequences.end();) {
    int64_t timeToLive = seq->transmitEnd() - OpenShock::millis();

    if (timeToLive > 0) {
      // Send the command
      rmt_transmit(channel, encoder, seq->payload(), seq->size() * sizeof(rmt_symbol_word_t), &txConfig);
      rmt_tx_wait_all_done(channel, kRmtTimeoutMs);
    } else {
      // Remove command if it has sent out its termination sequence for long enough
      if (timeToLive <= -kTerminatorDurationMs) {
        seq = sequences.erase(seq);
        continue;
      }

      // Send the termination sequence to stop the shocker
      rmt_transmit(channel, encoder, seq->terminator(), seq->size() * sizeof(rmt_symbol_word_t), &txConfig);
      rmt_tx_wait_all_done(channel, kRmtTimeoutMs);
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
        goto exit;  // Break out of nested loop so locals destruct before vTaskDelete
      }

      // Discard any command received while estopped
      if (OpenShock::EStopManager::IsEStopped()) {
        // Immediately break out to stop sequences; we can empty the queue later
        if (!wasEstopped) {
          break;
        }

        // Discard next item in queue
        continue;
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

    // Terminate all remaining sequences
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

    writeSequences(m_rmtChannel, m_rmtEncoder, sequences);
  }

exit:  // Locals (sequences) destruct here before task deletion
  vTaskDelete(nullptr);
}
