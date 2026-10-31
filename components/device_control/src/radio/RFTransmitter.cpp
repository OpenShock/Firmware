#include <freertos/FreeRTOS.h>

#include "radio/RFTransmitter.h"

const char* const TAG = "RFTransmitter";

#include "estop/EStopManager.h"
#include "Logging.h"

#include "radio/rmt/Sequence.h"
#include "Temporal.h"
#include <cstring>

#include <esp_err.h>
#include <soc/soc_caps.h>

#include <freertos/queue.h>

#include <algorithm>
#include <utility>
#include <vector>

const int32_t kRmtTimeoutMs = 100;
// After this many transmit failures in a row the RMT channel is presumed broken and the pending sequences are dropped
const uint32_t kMaxConsecutiveFailures = 20;
const uint32_t kMaxFailureBackoffMs    = 100;
const uint8_t kFlagOverwrite           = 1 << 0;
const uint8_t kFlagDeleteTask          = 1 << 1;
const uint8_t kFlagStop                = 1 << 2;
// A Stop waits longer for a slot than other commands: the queue only stays full while the task is mid-round
const TickType_t kStopQueueWait = pdMS_TO_TICKS(100);

using namespace OpenShock;

struct RFTransmitter::Command {
  int64_t transmitEnd;
  uint32_t seq;
  ShockerModelType modelType;
  ShockerCommandType type;
  uint16_t shockerId;
  uint8_t intensity;
  uint8_t flags;
};

static uint32_t shockerKey(ShockerModelType model, uint16_t shockerId)
{
  return (static_cast<uint32_t>(model) << 16) | shockerId;
}

RFTransmitter::RFTransmitter(gpio_num_t gpioPin)
  : m_txPin(gpioPin)
  , m_queueHandle(nullptr)
  , m_nextSeq(0)
  , m_rmtChannel(nullptr)
  , m_rmtEncoder(nullptr)
  , m_task(TAG, "RFTransmitter task")
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

  m_queueHandle = xQueueCreate(CONFIG_OPENSHOCK_RF_QUEUE_SIZE, sizeof(Command));
  if (m_queueHandle == nullptr) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create queue", m_txPin);
    destroy();
    return;
  }

  char name[32];
  snprintf(name, sizeof(name), "RFTransmitter-%u", m_txPin);

  if (!m_task.startExpensive(name, CONFIG_OPENSHOCK_RF_TASK_STACK_SIZE, CONFIG_OPENSHOCK_RF_TASK_PRIORITY, [this] { TransmitTask(); })) {
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

  uint8_t flags = overwriteExisting ? kFlagOverwrite : 0;

  // Stop logic
  bool isStop = type == ShockerCommandType::Stop;
  if (isStop) {
    OS_LOGV(TAG, "Stop command received");

    type       = ShockerCommandType::Vibrate;
    intensity  = 0;
    durationMs = 300;
    flags      = kFlagOverwrite | kFlagStop;
  } else {
    OS_LOGD(TAG, "Command received: %u %u %u %u", std::to_underlying(model), shockerId, std::to_underlying(type), intensity);
  }

  ScopedLock lock__(&m_sendMutex);  // Bounded: both queue sends below time out

  Command cmd = Command {
    .transmitEnd = OpenShock::millis() + durationMs,
    .seq         = m_nextSeq++,
    .modelType   = model,
    .type        = type,
    .shockerId   = shockerId,
    .intensity   = intensity,
    .flags       = flags,
  };

  // A Stop jumps the queue instead of waiting behind (or being dropped after) a backlog; the task then discards the
  // older commands for that shocker still queued behind it.
  BaseType_t queued = isStop ? xQueueSendToFront(m_queueHandle, &cmd, kStopQueueWait) : xQueueSend(m_queueHandle, &cmd, pdMS_TO_TICKS(10));
  if (queued != pdTRUE) {
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
  if (m_task.running()) {
    OS_LOGD(TAG, "[pin-%hhi] Stopping task", m_txPin);

    // The task blocks on its queue, so wake it with a kill command. Drop pending commands first so it always fits.
    m_task.stop([this] {
      ClearPendingCommands();

      Command cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.flags = kFlagDeleteTask;
      if (xQueueSendToFront(m_queueHandle, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        OS_LOGE(TAG, "[pin-%hhi] Failed to queue task kill command", m_txPin);
      }
    });

    OS_LOGD(TAG, "[pin-%hhi] Task stopped", m_txPin);

    // Clear the queue
    ClearPendingCommands();
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

// Returns false if the frame didn't go out. A failing rmt_transmit returns at once, so on failure this blocks for a
// growing back-off: the task runs above the network tasks and would otherwise spin, starving its core and flooding the
// log. `failures` counts failures in a row and is reset by a success.
static bool transmitSymbols(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, const rmt_symbol_word_t* symbols, size_t count, uint32_t& failures)
{
  rmt_transmit_config_t txConfig = {};
  txConfig.flags.eot_level       = 0;

  esp_err_t err = rmt_transmit(channel, encoder, symbols, count * sizeof(rmt_symbol_word_t), &txConfig);
  if (err == ESP_OK) {
    err = rmt_tx_wait_all_done(channel, kRmtTimeoutMs);  // Blocks by itself, so no back-off needed on its failure
    if (err == ESP_OK) {
      failures = 0;
      return true;
    }
    if (failures++ == 0) {
      OS_LOGE(TAG, "rmt_tx_wait_all_done failed: %s", esp_err_to_name(err));
    }
    // The frame may still be in flight, reading `symbols`, which the caller is free to rewrite or free once this
    // returns. Disabling the channel aborts it; enable it again for the next frame.
    rmt_disable(channel);
    rmt_enable(channel);
    return false;
  }

  // Log the first failure of a run and then every 10th
  if (failures++ % 10 == 0) {
    OS_LOGE(TAG, "rmt_transmit failed (%lu in a row): %s", static_cast<unsigned long>(failures), esp_err_to_name(err));
  }
  vTaskDelay(pdMS_TO_TICKS(std::min<uint32_t>(1U << std::min<uint32_t>(failures, 7), kMaxFailureBackoffMs)));
  return false;
}

static void writeSequences(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, std::vector<Rmt::Sequence>& sequences, const ManagedTask& task, uint32_t& failures, const TaskUtils::TaskWatchdog& watchdog)
{
  // Send queued commands
  for (auto seq = sequences.begin(); seq != sequences.end();) {
    watchdog.feed();  // Per frame: a round over many shockers can outlast the watchdog timeout
    if (failures >= kMaxConsecutiveFailures) {
      // Nothing is getting out; dropping them beats retrying forever. New commands get a fresh run.
      OS_LOGE(TAG, "RMT keeps failing, dropping %zu pending sequences", sequences.size());
      sequences.clear();
      failures = 0;
      return;
    }

    // A round of many sequences can outlast stop()'s timeout; the shocker stops on its own once frames stop.
    if (task.stopRequested()) {
      return;
    }

    int64_t now = OpenShock::millis();

    // A round can take several frames; re-check EStop per sequence so no payload goes out after activation.
    if (seq->transmitEnd() > now && OpenShock::EStopManager::IsEStopped()) {
      seq->setTransmitEnd(now);
    }

    int64_t timeToLive = seq->transmitEnd() - now;

    // Models with rolling state re-encode every frame; if that fails, terminate rather than resend a stale frame.
    if (timeToLive > 0 && !seq->refill(now)) {
      OS_LOGE(TAG, "Failed to re-encode frame for shocker %hu, terminating it", seq->shockerId());
      seq->setTransmitEnd(now);
      timeToLive = 0;
    }

    if (timeToLive > 0) {
      // Send the command
      transmitSymbols(channel, encoder, seq->payload(), seq->size(), failures);
      ++seq;
      continue;
    }

    // Remove command once it has sent out its termination sequence for long enough. A sequence that never got a
    // terminator out (a slow round skipped past the whole window, or a failed fill zeroed its end) sends one first.
    if (timeToLive <= -CONFIG_OPENSHOCK_RF_TERMINATOR_DURATION_MS && seq->terminatorSent()) {
      seq = sequences.erase(seq);
      continue;
    }

    // Send the termination sequence to stop the shocker. Only a terminator that went out counts: after a failure it is
    // retried next round, and kMaxConsecutiveFailures bounds the retries.
    if (transmitSymbols(channel, encoder, seq->terminator(), seq->size(), failures)) {
      seq->markTerminatorSent();
    }
    ++seq;
  }
}

void RFTransmitter::TransmitTask()
{
  OS_LOGD(TAG, "[pin-%hhi] RMT loop running on core %d", m_txPin, xPortGetCoreID());

  bool wasEstopped     = false;
  uint32_t rmtFailures = 0;  // Transmit failures in a row
  std::vector<Rmt::Sequence> sequences;
  // (shocker key, seq) of Stops taken from the front of the queue while older commands may still be behind them.
  // Cleared once the queue drains, so it stays as small as the number of distinct shockers stopped in one backlog.
  std::vector<std::pair<uint32_t, uint32_t>> stops;
  // On the task watchdog only while it has frames to send: idle, it blocks on its queue indefinitely
  TaskUtils::TaskWatchdog watchdog(false);
  while (true) {
    if (sequences.empty()) {
      watchdog.unsubscribe();
    }

    // Receive commands
    Command cmd;
    bool drained = true;
    while (xQueueReceive(m_queueHandle, &cmd, sequences.empty() ? portMAX_DELAY : 0) == pdTRUE) {
      // Destroy task if we receive destroy command
      if ((cmd.flags & kFlagDeleteTask) != 0) {
        return;
      }

      uint32_t key = shockerKey(cmd.modelType, cmd.shockerId);
      auto stop    = std::find_if(stops.begin(), stops.end(), [key](const auto& entry) { return entry.first == key; });
      if ((cmd.flags & kFlagStop) != 0) {
        if (stop == stops.end()) {
          stops.emplace_back(key, cmd.seq);
        } else if (static_cast<int32_t>(cmd.seq - stop->second) > 0) {
          stop->second = cmd.seq;
        }
      } else if (stop != stops.end() && static_cast<int32_t>(cmd.seq - stop->second) < 0) {
        continue;  // Sent before a Stop that overtook it
      }

      // Discard any command received while estopped
      if (OpenShock::EStopManager::IsEStopped()) {
        // Immediately break out to stop sequences; we can empty the queue later
        if (!wasEstopped) {
          drained = false;
          break;
        }

        // Discard next item in queue
        continue;
      }

      // EStop is clear when accepting a command; keep wasEstopped in sync so a re-trip is seen as a new edge.
      wasEstopped = false;

      if ((cmd.flags & kFlagOverwrite) != 0) {
        // Replace the sequence if it already exists
        if (modifySequence(sequences, cmd.modelType, cmd.shockerId, cmd.type, cmd.intensity, cmd.transmitEnd)) {
          continue;
        }
      }

      if (!addSequence(sequences, cmd.modelType, cmd.shockerId, cmd.type, cmd.intensity, cmd.transmitEnd)) {
        OS_LOGD(TAG, "[pin-%hhi] Failed to add sequence", m_txPin);
      }
    }

    if (drained) {
      stops.clear();
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

    if (!sequences.empty()) {
      watchdog.subscribe();
    }
    writeSequences(m_rmtChannel, m_rmtEncoder, sequences, m_task, rmtFailures, watchdog);
  }
}
