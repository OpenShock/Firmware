#pragma once

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
  class RFTransmitter {
  public:
    RFTransmitter(std::uint8_t gpioPin);
    ~RFTransmitter();

    inline std::uint8_t GetTxPin() const { return m_txPin; }

    inline bool ok() const { return m_rmtHandle != nullptr && m_queueHandle != nullptr && m_taskHandle != nullptr; }

    bool SendCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs, bool overwriteExisting = true);
    void ClearPendingCommands();

  private:
    void destroy();
    static void TransmitTask(void* arg);

    std::uint8_t m_txPin;
    rmt_obj_t* m_rmtHandle;
    QueueHandle_t m_queueHandle;
    TaskHandle_t m_taskHandle;
  };
}  // namespace OpenShock
