#include <freertos/FreeRTOS.h>

#include "led_drivers/RgbLedDriver.h"

const char* const TAG = "RGBLedDriver";

#include "BoardDescriptor.h"
#include "Chipset.h"
#include "Logging.h"

#include <esp_err.h>
#include <soc/soc_caps.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace OpenShock;

// Currently this assumes a single WS2812B LED
// TODO: Support multiple LEDs ?
// TODO: Support other LED types ?

RgbLedDriver::RgbLedDriver(gpio_num_t gpioPin)
  : m_gpioPin(GPIO_NUM_NC)
  , m_swapRg((Board::Get().flags & OPENSHOCK_BOARD_FLAG_LED_SWAP_RG) != 0)
  , m_brightness(65535)
  , m_rmtChannel(nullptr)
  , m_rmtEncoder(nullptr)
  , m_frame()
  , m_target()
  , m_ditherError()
  , m_txFailing(false)
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
  esp_err_t err = rmt_new_tx_channel(&channelConfig, &m_rmtChannel);
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
    rmt_disable(m_rmtChannel);
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

// Temporal dithering: the WS2812B takes 8 bits per channel, but the color and brightness are 16. Each frame shows the
// 8-bit step below the target or the one above, carrying the remainder (the low byte) into the next frame (a first-
// order sigma-delta), so averaged over frames the LED shows the 16-bit value. That gives fine steps where 8 bits are
// coarsest: low brightness, where 8% of a channel leaves only 21 steps. Frames go out every
// CONFIG_OPENSHOCK_RGB_LED_DITHER_INTERVAL_MS while some channel has a fraction; a whole 8-bit color is sent once.

void RgbLedDriver::apply(const RGBState16& state)
{
  const uint32_t brightness = m_brightness.load();
  const uint16_t channels[] = {state.red, state.green, state.blue};
  for (std::size_t i = 0; i < m_target.size(); ++i) {
    const uint32_t value = channels[i] * brightness / 65535;  // 0-65535
    // In 1/256ths of an 8-bit step: 65535 is 255 steps, so 255 * 256 = 65280 at most. Taking the high byte of the
    // 0-65535 value instead would make every level about 0.4% too bright (65535 / 256 is 255.996 steps).
    m_target[i] = static_cast<uint16_t>((value * 256 + 128) / 257);
  }
  refresh();
}

uint32_t RgbLedDriver::refreshIntervalMs() const
{
#if CONFIG_OPENSHOCK_RGB_LED_DITHERING
  for (uint16_t target : m_target) {
    if ((target & 0xFF) != 0) {
      return CONFIG_OPENSHOCK_RGB_LED_DITHER_INTERVAL_MS;
    }
  }
#endif
  return 0;  // Every channel is a whole 8-bit step: the LED holds it without refreshes
}

void RgbLedDriver::refresh()
{
  std::array<uint8_t, 3> out;
  for (std::size_t i = 0; i < m_target.size(); ++i) {
#if CONFIG_OPENSHOCK_RGB_LED_DITHERING
    const uint32_t sum = static_cast<uint32_t>(m_ditherError[i]) + (m_target[i] & 0xFF);
    m_ditherError[i]   = static_cast<uint8_t>(sum & 0xFF);
    out[i]             = static_cast<uint8_t>(std::min<uint32_t>((m_target[i] >> 8) + (sum >> 8), 255));
#else
    out[i] = static_cast<uint8_t>(std::min<uint32_t>((m_target[i] + 128) >> 8, 255));  // Nearest 8-bit step
#endif
  }
  sendFrame(out[0], out[1], out[2]);
}

void RgbLedDriver::sendFrame(uint8_t r, uint8_t g, uint8_t b)
{
  auto& led_data = m_frame;

  // WS2812B usually takes commands in GRB order
  // https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf - Page 5
  // But some actually expect RGB!
  // Board setting (descriptor flag), decided once in the constructor so this path stays a single branch.
  if (m_swapRg) {
    std::swap(r, g);
  }

  const uint32_t colors = (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(r) << 8) | static_cast<uint32_t>(b);

  // Encode the data
  for (std::size_t bit = 0; bit < 24; bit++) {
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

  // Send the data (blocking, wait up to 10ms per frame)
  rmt_transmit_config_t txConfig = {};
  txConfig.flags.eot_level       = 0;
  esp_err_t err                  = rmt_transmit(m_rmtChannel, m_rmtEncoder, led_data.data(), led_data.size() * sizeof(rmt_symbol_word_t), &txConfig);
  if (err != ESP_OK) {
    // Once per failure streak: dithering sends a frame every millisecond
    if (!m_txFailing) {
      OS_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
      m_txFailing = true;
    }
    return;
  }
  m_txFailing = false;
  if (rmt_tx_wait_all_done(m_rmtChannel, 10) != ESP_OK) {
    // Abort the frame still in flight before the buffer is rewritten for the next one
    rmt_disable(m_rmtChannel);
    rmt_enable(m_rmtChannel);
  }
}
