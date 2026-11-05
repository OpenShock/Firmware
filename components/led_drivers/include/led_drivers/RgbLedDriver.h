#pragma once

#include "led_drivers/PatternPlayer.h"
#include "OpenShock.h"

#include <hal/gpio_types.h>

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace OpenShock {
  // 8 bits per channel: what the pattern tables are written in
  struct RgbLedState {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint32_t duration;
  };

  // 16 bits per channel (0-65535). The WS2812B takes 8; the driver shows 10 by dithering over 4 frames (see apply()).
  struct RgbLedState16 {
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint32_t duration;
  };

  class RgbLedDriver : public PatternPlayer<RgbLedState16> {
    DISABLE_DEFAULT(RgbLedDriver);
    DISABLE_COPY(RgbLedDriver);
    DISABLE_MOVE(RgbLedDriver);

  public:
    using RGBState   = RgbLedState;
    using RGBState16 = RgbLedState16;

    /// @param brightness The starting brightness (0-65535), until SetBrightness16.
    RgbLedDriver(gpio_num_t gpioPin, uint16_t brightness);
    ~RgbLedDriver() override;

    bool IsValid() const { return m_gpioPin != GPIO_NUM_NC; }

    // 16-bit patterns
    using PatternPlayer<RgbLedState16>::SetPattern;

    // 8-bit patterns, widened to 16 bits per channel (255 -> 65535)
    void SetPattern(const RgbLedState* pattern, std::size_t patternLength);
    template<std::size_t N>
    inline void SetPattern(const RgbLedState (&pattern)[N])
    {
      SetPattern(pattern, N);
    }

    /// @brief 0-255; the same as SetBrightness16(brightness * 257).
    void SetBrightness(uint8_t brightness) { SetBrightness16(static_cast<uint16_t>(brightness * 257)); }
    /// @brief 0-65535. Takes effect with the next SetPattern, like the 8-bit form.
    void SetBrightness16(uint16_t brightness) { m_brightness.store(brightness); }

  protected:
    void apply(const RGBState16& state) override;
    bool enterSleep() override;
    void exitSleep() override;

  private:
    static constexpr std::size_t kSymbolsPerFrame = 24;  // 24 bits per LED (8 bits per color * 3 colors)
    static constexpr std::size_t kDitherFrames    = 4;   // Steps between two 8-bit levels: 4 is 2 bits more, 10-bit output

    void encodeFrame(std::size_t index, uint8_t red, uint8_t green, uint8_t blue, bool latchGap);
    bool transmit(std::size_t frames, bool loop);  // False when the frames may not have gone out
    void abortAndLatch();                          // Stops the channel mid-transmission, then lets the LED latch

    gpio_num_t m_gpioPin;
    bool m_swapRg;  // Board wants RGB rather than GRB wire order; decided once, apply() runs on the pattern task
    std::atomic<uint16_t> m_brightness;
    rmt_channel_handle_t m_rmtChannel;
    rmt_encoder_handle_t m_rmtEncoder;
    bool m_canLoop;  // The channel holds all dither frames in RMT memory, so the hardware can repeat them
    bool m_looping;  // Pattern task only; a looped transmission is running and must be stopped before the next one
    // The frames RMT reads from. A member, not a local: after a timed-out wait the transmission may still be reading it.
    std::array<rmt_symbol_word_t, kSymbolsPerFrame * kDitherFrames> m_frames;
    rmt_symbol_word_t m_latchSymbol;  // The line held low, sent after an abort; a member for the same reason
  };
}  // namespace OpenShock
