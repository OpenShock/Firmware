#include <freertos/FreeRTOS.h>

#include "visual/MonoLedDriver.h"

const char* const TAG = "MonoLedDriver";

#include "Chipset.h"
#include "Logging.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

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
  , m_pattern()
  , m_taskHandle(nullptr)
  , m_taskMutex()
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
  };

  ledc_timer_config(&ledc_config);  // TODO: Error handling

  ledc_channel_config_t ledc_channel = {
    .gpio_num   = gpioPin,
    .speed_mode = OS_LEDC_SPEED,
    .channel    = OS_LEDC_CHANNEL,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = OS_LEDC_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };

  ledc_channel_config(&ledc_channel);  // TODO: Error handling

  m_gpioPin = gpioPin;
}

MonoLedDriver::~MonoLedDriver()
{
  ClearPattern();

  ledc_stop(OS_LEDC_SPEED, OS_LEDC_CHANNEL, 0);  // TODO: Error handling
}

void MonoLedDriver::SetPattern(const State* pattern, std::size_t patternLength)
{
  m_taskMutex.lock(portMAX_DELAY);

  ClearPatternInternal();

  // Set new values
  m_pattern.resize(patternLength);
  std::copy(pattern, pattern + patternLength, m_pattern.begin());

  char name[32];
  snprintf(name, sizeof(name), "MonoLedDriver-%d", m_gpioPin);

  // Start the task
  m_stopRequested.store(false, std::memory_order_relaxed);
  BaseType_t result = TaskUtils::TaskCreateUniversal(Util::FnProxy<&MonoLedDriver::RunPattern>, name, 1024, this, 1, &m_taskHandle, 1);  // PROFILED: 0.5KB stack usage
  if (result != pdPASS) {
    OS_LOGE(TAG, "[pin-%d] Failed to create task: %d", m_gpioPin, result);

    m_taskHandle = nullptr;
    m_pattern.clear();
  }

  m_taskMutex.unlock();
}

void MonoLedDriver::ClearPattern()
{
  m_taskMutex.lock(portMAX_DELAY);

  ClearPatternInternal();

  m_taskMutex.unlock();
}

void MonoLedDriver::SetBrightness(uint8_t brightness)
{
  m_brightness = brightness;
}

void MonoLedDriver::ClearPatternInternal()
{
  if (m_taskHandle != nullptr) {
    m_stopRequested.store(true, std::memory_order_relaxed);
    TaskUtils::StopTask(m_taskHandle, TAG, "MonoLedDriver task");
    m_taskHandle = nullptr;
  }

  m_pattern.clear();
}

void MonoLedDriver::RunPattern()
{
  while (!m_stopRequested.load(std::memory_order_relaxed)) {
    for (const auto& state : m_pattern) {
      if (m_stopRequested.load(std::memory_order_relaxed)) break;
      ledc_set_duty(OS_LEDC_SPEED, OS_LEDC_CHANNEL, state.level ? m_brightness : 0);
      ledc_update_duty(OS_LEDC_SPEED, OS_LEDC_CHANNEL);
      vTaskDelay(pdMS_TO_TICKS(state.duration));
    }
  }

  vTaskDelete(nullptr);
}
