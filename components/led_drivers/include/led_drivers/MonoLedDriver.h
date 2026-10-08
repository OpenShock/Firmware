#pragma once

#include "led_drivers/PatternPlayer.h"
#include "OpenShock.h"

#include <hal/gpio_types.h>

#include <atomic>
#include <cstdint>

namespace OpenShock {
  struct MonoLedState {
    bool level;
    uint32_t duration;
  };

  class MonoLedDriver : public PatternPlayer<MonoLedState> {
    DISABLE_DEFAULT(MonoLedDriver);
    DISABLE_COPY(MonoLedDriver);
    DISABLE_MOVE(MonoLedDriver);

  public:
    using State = MonoLedState;

    MonoLedDriver(gpio_num_t gpioPin);
    ~MonoLedDriver() override;

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    void SetBrightness(uint8_t brightness) { m_brightness.store(brightness); }

  protected:
    void apply(const State& state) override;

  private:
    gpio_num_t m_gpioPin;
    std::atomic<uint8_t> m_brightness;
  };
}  // namespace OpenShock
