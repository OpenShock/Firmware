#pragma once

#include <cstdint>

struct rmt_obj_s;
typedef rmt_obj_s rmt_obj_t;

struct QueueDefinition;
typedef QueueDefinition* QueueHandle_t;

typedef void* TaskHandle_t;

namespace ShockLink {
  class RFTransmitter {
  public:
    RFTransmitter(int gpioPin, int queueSize = 32);
    ~RFTransmitter();

    inline bool ok() const { return m_taskHandle != nullptr && m_queueHandle != nullptr; }

    bool SendCommand(std::uint8_t shockerModel,
                     std::uint16_t shockerId,
                     std::uint8_t method,
                     std::uint8_t intensity,
                     std::uint32_t duration);
    void ClearPendingCommands();

  private:
    static void TransmitTask(void* arg);

    int m_gpioPin;
    char m_name[32];
    rmt_obj_t* m_rmtHandle;
    QueueHandle_t m_queueHandle;
    TaskHandle_t m_taskHandle;
  };
}  // namespace ShockLink
