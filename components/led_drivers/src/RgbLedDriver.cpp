#include <freertos/FreeRTOS.h>

#include "led_drivers/RgbLedDriver.h"

const char* const TAG = "RGBLedDriver";

#include "Chipset.h"
#include "Logging.h"

#include <esp_err.h>
#include <soc/soc_caps.h>

#include <array>

// R/G channel swap is a board setting, generated into openshock_board.h by
// scripts/gen_env_header.py rather than coming from Kconfig - keeping it out of
// sdkconfig.h is what lets boards sharing a chip reuse ESP-IDF objects. It is always
// defined, as 0 or 1, so the #if below reads it directly.
#include "openshock_board.h"
#include "sdkconfig.h"

using namespace OpenShock;

// Currently this assumes a single WS2812B LED
// TODO: Support multiple LEDs ?
// TODO: Support other LED types ?

RgbLedDriver::RgbLedDriver(gpio_num_t gpioPin)
  : m_gpioPin(GPIO_NUM_NC)
  , m_brightness(255)
  , m_rmtChannel(nullptr)
  , m_rmtEncoder(nullptr)
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

void RgbLedDriver::apply(const RGBState& state)
{
  std::array<rmt_symbol_word_t, 24> led_data;  // 24 bits per LED (8 bits per color * 3 colors)

  // WS2812B usually takes commands in GRB order
  // https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf - Page 5
  // But some actually expect RGB!
  const uint16_t brightness = m_brightness.load();

  uint8_t r = static_cast<uint8_t>(static_cast<uint16_t>(state.red) * brightness / 255);
  uint8_t g = static_cast<uint8_t>(static_cast<uint16_t>(state.green) * brightness / 255);
  uint8_t b = static_cast<uint8_t>(static_cast<uint16_t>(state.blue) * brightness / 255);
#if OPENSHOCK_LED_SWAP_RG_CHANNELS
  std::swap(r, g);
#endif

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
  rmt_transmit(m_rmtChannel, m_rmtEncoder, led_data.data(), led_data.size() * sizeof(rmt_symbol_word_t), &txConfig);
  rmt_tx_wait_all_done(m_rmtChannel, 10);
}
