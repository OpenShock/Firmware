#pragma once

#include "enums/ShockerCommandType.h"
#include "enums/ShockerModelType.h"
#include "OpenShock.h"
#include "util/ManagedTask.h"

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <hal/gpio_types.h>

#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>

namespace OpenShock {
  class RFTransmitter {
    DISABLE_COPY(RFTransmitter);
    DISABLE_MOVE(RFTransmitter);

  public:
    RFTransmitter(gpio_num_t gpioPin);
    ~RFTransmitter();

    inline gpio_num_t GetTxPin() const { return m_txPin; }

    inline bool ok() const { return m_txPin != GPIO_NUM_NC && m_queueHandle != nullptr && m_task.running(); }

    bool SendCommand(ShockerModelType model, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint16_t durationMs, bool overwriteExisting = true);
    void ClearPendingCommands();

  private:
    void destroy();
    void TransmitTask();

    struct Command;

    gpio_num_t m_txPin;
    QueueHandle_t m_queueHandle;
    rmt_channel_handle_t m_rmtChannel;
    rmt_encoder_handle_t m_rmtEncoder;
    ManagedTask m_task;
  };
}  // namespace OpenShock
