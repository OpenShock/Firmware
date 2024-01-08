#pragma once

#include "Common.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp32-hal-rmt.h>

#include <array>
#include <cstdint>

namespace OpenShock {
  class RGBPatternManager {
    DISABLE_COPY(RGBPatternManager);
  public:
    RGBPatternManager(std::uint8_t gpioPin);
    ~RGBPatternManager();

    struct RGBState {
      std::uint8_t red;
      std::uint8_t green;
      std::uint8_t blue;
      std::uint32_t duration;
    };

    void SetPattern(const RGBState* pattern, std::size_t patternLength);
    template <std::size_t N>
    inline void SetPattern(const RGBState (&pattern)[N]) {
      SetPattern(pattern, N);
    }
    void SetBrightness(std::uint8_t brightness);
    void ClearPattern();

  private:
    void ClearPatternInternal();
    void SendRGB(const RGBState& state);
    static void RunPattern(void* arg);

    std::uint8_t m_rgbPin;
    std::uint8_t m_rgbBrightness;  // 0-255
    rmt_obj_t* m_rmtHandle;
    RGBState* m_pattern;
    std::size_t m_patternLength;
    TaskHandle_t m_taskHandle;
    SemaphoreHandle_t m_taskMutex;
  };
}  // namespace OpenShock
