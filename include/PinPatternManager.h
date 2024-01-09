#pragma once

#include "Common.h"

#include <hal/gpio_types.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <vector>

namespace OpenShock {
  class PinPatternManager {
    DISABLE_COPY(PinPatternManager);

  public:
    struct State {
      bool level;
      std::uint32_t duration;
    };

    PinPatternManager() = delete;
    PinPatternManager(gpio_num_t gpioPin);
    ~PinPatternManager();

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    void SetPattern(const State* pattern, std::size_t patternLength);
    template<std::size_t N>
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
