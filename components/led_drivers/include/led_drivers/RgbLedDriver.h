#pragma once

#include "OpenShock.h"
#include "SimpleMutex.h"

#include <hal/gpio_types.h>

#include <freertos/task.h>

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace OpenShock {
  class RgbLedDriver {
    DISABLE_DEFAULT(RgbLedDriver);
    DISABLE_COPY(RgbLedDriver);
    DISABLE_MOVE(RgbLedDriver);

  public:
    RgbLedDriver(gpio_num_t gpioPin);
    ~RgbLedDriver();

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    struct RGBState {
      uint8_t red;
      uint8_t green;
      uint8_t blue;
      uint32_t duration;
    };

    void SetPattern(const RGBState* pattern, std::size_t patternLength);
    template<std::size_t N>
    inline void SetPattern(const RGBState (&pattern)[N])
    {
      SetPattern(pattern, N);
    }
    void ClearPattern();

    void SetBrightness(uint8_t brightness);

  private:
    void ClearPatternInternal();
    void RunPattern();

    gpio_num_t m_gpioPin;
    uint8_t m_brightness;  // 0-255
    std::vector<RGBState> m_pattern;
    TaskHandle_t m_taskHandle;
    std::atomic<bool> m_taskExited;  // TaskUtils::TaskExitFlag; set by the task just before it deletes itself
    SimpleMutex m_taskMutex;
    std::atomic<bool> m_stopRequested {false};
    rmt_channel_handle_t m_rmtChannel;
    rmt_encoder_handle_t m_rmtEncoder;
  };
}  // namespace OpenShock
