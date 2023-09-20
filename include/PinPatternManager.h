#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <span>
#include <cstdint>

namespace ShockLink {
  class PinPatternManager {
  public:
    PinPatternManager(unsigned int pin);
    ~PinPatternManager();

    struct State {
      bool level;
      std::uint32_t duration;
    };

    static void SetPattern(std::span<const State> pattern);
    static void ClearPattern();
  private:
    void ClearPatternInternal();
    static void RunPattern(void* arg);

    unsigned int m_pin;
    State* m_pattern;
    std::size_t m_patternLength;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskSemaphore;
  };
}
