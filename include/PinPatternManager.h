#pragma once

#include "Common.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <array>
#include <cstdint>

namespace OpenShock {
  class PinPatternManager {
    DISABLE_COPY(PinPatternManager);
  public:
    PinPatternManager(std::uint8_t gpioPin);
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

    std::uint8_t m_gpioPin;
    State* m_pattern;
    std::size_t m_patternLength;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskMutex;
  };
}  // namespace OpenShock
