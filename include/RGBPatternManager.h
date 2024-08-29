#pragma once

#include "Common.h"

#include <hal/gpio_types.h>

#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp32-hal-rmt.h>

#include <cstdint>
#include <vector>

namespace OpenShock {
  class RGBPatternManager {
    DISABLE_COPY(RGBPatternManager);

  public:
    RGBPatternManager() = delete;
    RGBPatternManager(gpio_num_t gpioPin);
    ~RGBPatternManager();

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    struct RGBState {
      uint8_t red;
      uint8_t green;
      uint8_t blue;
      uint32_t duration;
    };

    void SetPattern(const RGBState* pattern, std::size_t patternLength);
    template<std::size_t N>
    inline void SetPattern(const RGBState (&pattern)[N]) {
      SetPattern(pattern, N);
    }
    void SetBrightness(uint8_t brightness);
    void ClearPattern();

  private:
    void ClearPatternInternal();
    static void RunPattern(void* arg);

    gpio_num_t m_gpioPin;
    uint8_t m_brightness;  // 0-255
    std::vector<RGBState> m_pattern;
    rmt_obj_t* m_rmtHandle;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskMutex;
  };
}  // namespace OpenShock
