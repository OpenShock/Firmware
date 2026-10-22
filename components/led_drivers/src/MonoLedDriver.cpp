#include <freertos/FreeRTOS.h>

#include "led_drivers/MonoLedDriver.h"

const char* const TAG = "MonoLedDriver";

#include "Chipset.h"
#include "Logging.h"

#include <driver/ledc.h>

#define OS_LEDC_TIMER LEDC_TIMER_0
#ifdef SOC_LEDC_SUPPORT_HS_MODE
#define OS_LEDC_SPEED LEDC_HIGH_SPEED_MODE
#else
#define OS_LEDC_SPEED LEDC_LOW_SPEED_MODE
#endif
#define OS_LEDC_CHANNEL    LEDC_CHANNEL_0
#define OS_LEDC_RESOLUTION LEDC_TIMER_8_BIT
#define OS_LEDC_FREQUENCY  4000  // https://en.wikipedia.org/wiki/Flicker_fusion_threshold

using namespace OpenShock;

MonoLedDriver::MonoLedDriver(gpio_num_t gpioPin)
  : m_gpioPin(GPIO_NUM_NC)
  , m_brightness(255)
{
  if (gpioPin == GPIO_NUM_NC) {
    OS_LOGE(TAG, "Pin is not set");
    return;
  }

  if (!IsValidOutputPin(gpioPin)) {
    OS_LOGE(TAG, "Pin %d is not a valid output pin", gpioPin);
    return;
  }

  ledc_timer_config_t ledc_config = {
    .speed_mode      = OS_LEDC_SPEED,
    .duty_resolution = OS_LEDC_RESOLUTION,
    .timer_num       = OS_LEDC_TIMER,
    .freq_hz         = OS_LEDC_FREQUENCY,
    .clk_cfg         = LEDC_AUTO_CLK,
    .deconfigure     = false,
  };

  ledc_timer_config(&ledc_config);  // TODO: Error handling

  ledc_channel_config_t ledc_channel = {
    .gpio_num    = gpioPin,
    .speed_mode  = OS_LEDC_SPEED,
    .channel     = OS_LEDC_CHANNEL,
    .intr_type   = LEDC_INTR_DISABLE,
    .timer_sel   = OS_LEDC_TIMER,
    .duty        = 0,
    .hpoint      = 0,
    .sleep_mode  = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,  // default: no output during light-sleep
    .flags       = {},
    .deconfigure = false,
  };

  ledc_channel_config(&ledc_channel);  // TODO: Error handling

  char name[32];
  snprintf(name, sizeof(name), "MonoLedDriver-%d", gpioPin);
  if (!startTask(name, 1536)) {  // PROFILED (old per-pattern task): 0.5KB stack usage
    OS_LOGE(TAG, "[pin-%d] Failed to create task", gpioPin);
    ledc_stop(OS_LEDC_SPEED, OS_LEDC_CHANNEL, 0);
    return;
  }

  m_gpioPin = gpioPin;
}

MonoLedDriver::~MonoLedDriver()
{
  // Before the LEDC channel goes away
  stopTask();

  ledc_stop(OS_LEDC_SPEED, OS_LEDC_CHANNEL, 0);  // TODO: Error handling
}

void MonoLedDriver::apply(const State& state)
{
  ledc_set_duty(OS_LEDC_SPEED, OS_LEDC_CHANNEL, state.level ? m_brightness.load() : 0);
  ledc_update_duty(OS_LEDC_SPEED, OS_LEDC_CHANNEL);
}
