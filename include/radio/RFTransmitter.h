#pragma once

#include "Constants.h"
#include "radio/rmt/MainEncoder.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

// Forward definitions to remove clutter
struct rmt_obj_s;
typedef rmt_obj_s rmt_obj_t;
struct QueueDefinition;
typedef QueueDefinition* QueueHandle_t;
typedef void* TaskHandle_t;

namespace OpenShock {
  struct command_t {
    std::int64_t timestamp;
    std::vector<rmt_data_t> sequence;
    std::shared_ptr<std::vector<rmt_data_t>> zeroSequence;
    std::uint16_t shockerId;
    bool isKeepAlive;
  };
  class RFTransmitter {
  public:
    RFTransmitter(std::uint8_t gpioPin, int queueSize = 32);
    ~RFTransmitter();

    inline std::uint8_t GetTxPin() const { return m_txPin; }

    inline bool ok() const { return m_rmtHandle != nullptr && m_transmitQueueHandle != nullptr && m_transmitTaskHandle != nullptr; }

    bool SendCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs);
    void ClearPendingCommands();

  private:
    void destroy();
    static void TransmitTask(void* arg);
    static void KeepAliveTask(void* arg);
    static void replaceOrAddCommand(std::vector<command_t*>& commands, command_t* newCmd, bool checkKeepAlive, bool createCopy);

    std::uint8_t m_txPin;
    rmt_obj_t* m_rmtHandle;
    QueueHandle_t m_transmitQueueHandle;
    TaskHandle_t m_transmitTaskHandle;
    QueueHandle_t m_keepAliveQueueHandle;
    TaskHandle_t m_keepAliveTaskHandle;
  };
}  // namespace OpenShock
