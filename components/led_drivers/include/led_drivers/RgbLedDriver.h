#pragma once

#include "led_drivers/PatternPlayer.h"
#include "OpenShock.h"

#include <hal/gpio_types.h>

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

#include <atomic>
#include <cstdint>

namespace OpenShock {
  struct RgbLedState {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t duration;
  };

  class RgbLedDriver : public PatternPlayer<RgbLedState> {
    DISABLE_DEFAULT(RgbLedDriver);
    DISABLE_COPY(RgbLedDriver);
    DISABLE_MOVE(RgbLedDriver);

  public:
    using RGBState = RgbLedState;

    RgbLedDriver(gpio_num_t gpioPin);
    ~RgbLedDriver() override;

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    // Range: 0-255
    void SetBrightness(uint8_t brightness) { m_brightness.store(brightness); }

  protected:
    void apply(const RGBState& state) override;

  private:
    gpio_num_t m_gpioPin;
    std::atomic<uint8_t> m_brightness;
    rmt_channel_handle_t m_rmtChannel;
    rmt_encoder_handle_t m_rmtEncoder;
  };
}  // namespace OpenShock
