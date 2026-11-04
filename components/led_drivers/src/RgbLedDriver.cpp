#include <freertos/FreeRTOS.h>

#include "led_drivers/RgbLedDriver.h"

const char* const TAG = "RGBLedDriver";

#include "BoardDescriptor.h"
#include "Chipset.h"
#include "Logging.h"

#include <esp_err.h>
#include <hal/rmt_ll.h>  // RMT_LL_TX_CANDIDATES_PER_INST
#include <soc/soc_caps.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace OpenShock;

// Dithering: the WS2812B takes 8 bits per color. The driver shows more by sending kDitherFrames frames that each round
// a channel down or up to a neighbouring 8-bit level, so their average lands on the step in between (kDitherFrames = 4:
// quarter steps, 10-bit output), and has the RMT hardware repeat them on its own (a looped transmission) for as long
// as the color holds: no CPU work after apply().

// Low time after each frame so the LED latches it: over 280 us for the WS2812B-V5 (older parts need 50 us)
static constexpr uint16_t kLatchTicks = 3000;  // 300 us at 10 MHz

// Currently this assumes a single WS2812B LED
// TODO: Support multiple LEDs ?
// TODO: Support other LED types ?

RgbLedDriver::RgbLedDriver(gpio_num_t gpioPin, uint16_t brightness)
  : m_gpioPin(GPIO_NUM_NC)
  , m_swapRg((Board::Get().flags & OPENSHOCK_BOARD_FLAG_LED_SWAP_RG) != 0)
  , m_brightness(brightness)
  , m_rmtChannel(nullptr)
  , m_rmtEncoder(nullptr)
  , m_canLoop(false)
  , m_looping(false)
  , m_frames()
{
  if (gpioPin == GPIO_NUM_NC) {
    OS_LOGE(TAG, "Pin is not set");
    return;
  }

  if (!OpenShock::IsValidOutputPin(gpioPin)) {
    OS_LOGE(TAG, "Pin %hhi is not a valid output pin", gpioPin);
    return;
  }

  // 10 MHz resolution => 100 ns tick (WS2812B timing).
  rmt_tx_channel_config_t channelConfig = {
    .gpio_num          = gpioPin,
    .clk_src           = RMT_CLK_SRC_DEFAULT,
    .resolution_hz     = 10'000'000,
    .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,  // exactly 1 memory block
    .trans_queue_depth = 4,
    .intr_priority     = 0,                              // 0 => driver picks a low/medium priority
    .flags             = {},
  };
  // A looped transmission must fit in the channel's RMT memory with an end marker, so the channel takes several memory
  // blocks. Blocks are contiguous and channels are taken lowest first, so this channel (created before the RF
  // transmitter's) occupies the blocks of the channels after it; that leaves the RF transmitter a channel only on chips
  // with more transmit channels than those blocks. On the others (ESP32-C3) the LED rounds to 8 bits.
  constexpr std::size_t kLoopSymbols   = kSymbolsPerFrame * kDitherFrames + 1;  // The frames and the end marker
  constexpr std::size_t kLoopMemBlocks = (kLoopSymbols + SOC_RMT_MEM_WORDS_PER_CHANNEL - 1) / SOC_RMT_MEM_WORDS_PER_CHANNEL;
  constexpr bool kLoopDitherFits       = CONFIG_OPENSHOCK_RGB_LED_DITHERING && kLoopMemBlocks < RMT_LL_TX_CANDIDATES_PER_INST;

  esp_err_t err = ESP_FAIL;
  if constexpr (kLoopDitherFits) {
    // Enough memory blocks for the dither frames; on failure, fall back to one block and 8-bit output
    channelConfig.mem_block_symbols = kLoopMemBlocks * SOC_RMT_MEM_WORDS_PER_CHANNEL;
    err                             = rmt_new_tx_channel(&channelConfig, &m_rmtChannel);
    m_canLoop                       = err == ESP_OK;
    if (!m_canLoop) {
      OS_LOGW(TAG, "No room for the dithered RMT channel (%s), the RGB LED shows 8 bits per color", esp_err_to_name(err));
      channelConfig.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    }
  }
  if (!m_canLoop) {
    err = rmt_new_tx_channel(&channelConfig, &m_rmtChannel);
  }
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to create RMT TX channel for pin %hhi: %s", gpioPin, esp_err_to_name(err));
    m_rmtChannel = nullptr;
    return;
  }

  rmt_copy_encoder_config_t encoderConfig = {};
  err                                     = rmt_new_copy_encoder(&encoderConfig, &m_rmtEncoder);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to create RMT copy encoder for pin %hhi: %s", gpioPin, esp_err_to_name(err));
    rmt_del_channel(m_rmtChannel);
    m_rmtChannel = nullptr;
    m_rmtEncoder = nullptr;
    return;
  }

  err = rmt_enable(m_rmtChannel);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to enable RMT channel for pin %hhi: %s", gpioPin, esp_err_to_name(err));
    rmt_del_encoder(m_rmtEncoder);
    rmt_del_channel(m_rmtChannel);
    m_rmtChannel = nullptr;
    m_rmtEncoder = nullptr;
    return;
  }

  if (!startTask(TAG, 4096)) {  // PROFILED (old per-pattern task): 1.7KB stack usage
    OS_LOGE(TAG, "[pin-%hhi] Failed to create task", gpioPin);
    rmt_disable(m_rmtChannel);
    rmt_del_encoder(m_rmtEncoder);
    rmt_del_channel(m_rmtChannel);
    m_rmtChannel = nullptr;
    m_rmtEncoder = nullptr;
    return;
  }

  m_gpioPin = gpioPin;
}

RgbLedDriver::~RgbLedDriver()
{
  stopTask();  // before the RMT channel goes away

  if (m_rmtChannel != nullptr) {
    if (!isAsleep()) {  // Asleep, the channel is already disabled
      rmt_disable(m_rmtChannel);
    }
    rmt_del_channel(m_rmtChannel);
    m_rmtChannel = nullptr;
  }
  if (m_rmtEncoder != nullptr) {
    rmt_del_encoder(m_rmtEncoder);
    m_rmtEncoder = nullptr;
  }
}

void RgbLedDriver::SetPattern(const RgbLedState* pattern, std::size_t patternLength)
{
  std::vector<RgbLedState16> wide;
  wide.reserve(patternLength);
  for (std::size_t i = 0; i < patternLength; ++i) {
    const RgbLedState& s = pattern[i];
    // x * 257 maps 0-255 onto 0-65535 exactly (255 * 257 = 65535)
    wide.push_back({static_cast<uint16_t>(s.red * 257), static_cast<uint16_t>(s.green * 257), static_cast<uint16_t>(s.blue * 257), s.duration});
  }
  PatternPlayer<RgbLedState16>::SetPattern(wide.data(), wide.size());
}

void RgbLedDriver::apply(const RGBState16& state)
{
  const uint32_t brightness = m_brightness.load();
  const uint16_t channels[] = {state.red, state.green, state.blue};

  // Each channel in 1/kDitherFrames steps of the LED's 8-bit levels: the 8-bit level and a fraction of kDitherFrames
  constexpr uint32_t kSteps = kDitherFrames;
  std::array<uint32_t, 3> steps;
  bool fractional = false;
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const uint32_t value = channels[i] * brightness / 65535;  // 0-65535
    steps[i]             = (value * (255 * kSteps) + 32767) / 65535;
    fractional |= (steps[i] % kSteps) != 0;
  }

  if (!m_canLoop || !fractional) {
    // A whole 8-bit color (or no room to loop): one frame, the nearest level
    auto level = [](uint32_t step) { return static_cast<uint8_t>(std::min<uint32_t>((step + kSteps / 2) / kSteps, 255)); };
    encodeFrame(0, level(steps[0]), level(steps[1]), level(steps[2]), false);
    transmit(1, false);
    return;
  }

  // Spread each channel's fraction over the frames: with 4, 1/4 is one frame a level up, 2/4 every other frame, 3/4 three
  std::array<uint32_t, 3> error = {};
  for (std::size_t frame = 0; frame < kDitherFrames; ++frame) {
    std::array<uint8_t, 3> out;
    for (std::size_t i = 0; i < out.size(); ++i) {
      error[i] += steps[i] % kSteps;
      const uint32_t up = error[i] >= kSteps ? 1 : 0;
      error[i] -= up * kSteps;
      out[i] = static_cast<uint8_t>(steps[i] / kSteps + up);  // At most 254 + 1: a whole 255 has no fraction
    }
    encodeFrame(frame, out[0], out[1], out[2], true);
  }
  transmit(kDitherFrames, true);
}

void RgbLedDriver::enterSleep()
{
  // One black frame so the LED latches off (it holds its last color), then the channel disabled: that stops any looped
  // transmission and releases the RMT's clock and power management lock until exitSleep
  encodeFrame(0, 0, 0, 0, false);
  transmit(1, false);
  rmt_disable(m_rmtChannel);
}

void RgbLedDriver::exitSleep()
{
  esp_err_t err = rmt_enable(m_rmtChannel);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to enable the RMT channel after sleep: %s", esp_err_to_name(err));
  }
}

void RgbLedDriver::encodeFrame(std::size_t index, uint8_t r, uint8_t g, uint8_t b, bool latchGap)
{
  rmt_symbol_word_t* led_data = m_frames.data() + index * kSymbolsPerFrame;

  // WS2812B usually takes commands in GRB order
  // https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf - Page 5
  // But some actually expect RGB!
  // Board setting (descriptor flag), decided once in the constructor so this path stays a single branch.
  if (m_swapRg) {
    std::swap(r, g);
  }

  const uint32_t colors = (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(r) << 8) | static_cast<uint32_t>(b);

  // Encode the data
  for (std::size_t bit = 0; bit < kSymbolsPerFrame; bit++) {
    if (colors & (1 << (23 - bit))) {
      led_data[bit].level0    = 1;
      led_data[bit].duration0 = 8;
      led_data[bit].level1    = 0;
      led_data[bit].duration1 = 4;
    } else {
      led_data[bit].level0    = 1;
      led_data[bit].duration0 = 4;
      led_data[bit].level1    = 0;
      led_data[bit].duration1 = 8;
    }
  }

  // Looped frames run back to back: hold the line low after the last bit so the LED latches each one
  if (latchGap) {
    led_data[kSymbolsPerFrame - 1].duration1 = kLatchTicks;
  }
}

void RgbLedDriver::transmit(std::size_t frames, bool loop)
{
  // A looped transmission never finishes on its own; stopping the channel ends it (possibly mid-frame, which the next
  // frame overwrites within a millisecond)
  if (m_looping) {
    rmt_disable(m_rmtChannel);
    rmt_enable(m_rmtChannel);
    m_looping = false;
  }

  rmt_transmit_config_t txConfig = {};
  txConfig.loop_count            = loop ? -1 : 0;  // -1: repeat until stopped
  txConfig.flags.eot_level       = 0;
  esp_err_t err                  = rmt_transmit(m_rmtChannel, m_rmtEncoder, m_frames.data(), frames * kSymbolsPerFrame * sizeof(rmt_symbol_word_t), &txConfig);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
    return;
  }
  if (loop) {
    m_looping = true;
    return;
  }

  // Blocking, wait up to 10ms for the frame
  if (rmt_tx_wait_all_done(m_rmtChannel, 10) != ESP_OK) {
    // Abort the frame still in flight before the buffer is rewritten for the next one
    rmt_disable(m_rmtChannel);
    rmt_enable(m_rmtChannel);
  }
}
