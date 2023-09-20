#pragma once

#include <nonstd/span.hpp>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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

    void SetPattern(nonstd::span<const State> pattern);
    void ClearPattern();

  private:
    void ClearPatternInternal();
    static void RunPattern(void* arg);

    unsigned int m_pin;
    State* m_pattern;
    std::size_t m_patternLength;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskSemaphore;
  };
}  // namespace ShockLink
