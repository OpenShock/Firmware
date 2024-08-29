#pragma once

#include "Common.h"

#include <hal/gpio_types.h>

#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <vector>

namespace OpenShock {
  class MonoLedDriver {
    DISABLE_COPY(MonoLedDriver);

  public:
    struct State {
      bool level;
      uint32_t duration;
    };

    MonoLedDriver() = delete;
    MonoLedDriver(gpio_num_t gpioPin);
    ~MonoLedDriver();

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    void SetPattern(const State* pattern, std::size_t patternLength);
    template<std::size_t N>
    inline void SetPattern(const State (&pattern)[N]) {
      SetPattern(pattern, N);
    }
    void ClearPattern();

    void SetBrightness(uint8_t brightness);

  private:
    void ClearPatternInternal();
    static void RunPattern(void* arg);

    gpio_num_t m_gpioPin;
    uint8_t m_brightness;
    std::vector<State> m_pattern;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskMutex;
  };
}  // namespace OpenShock
