#pragma once

#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <vector>

namespace OpenShock {
  class PinPatternManager {
  public:
    PinPatternManager(gpio_num_t gpioPin);
    ~PinPatternManager();

    struct State {
      bool level;
      std::uint32_t duration;
    };

    void SetPattern(const State* pattern, std::size_t patternLength);
    template <std::size_t N>
    inline void SetPattern(const State (&pattern)[N]) {
      SetPattern(pattern, N);
    }
    void ClearPattern();

  private:
    void ClearPatternInternal();
    static void RunPattern(void* arg);

    gpio_num_t m_gpioPin;
    std::vector<State> m_pattern;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskMutex;
  };
}  // namespace OpenShock
