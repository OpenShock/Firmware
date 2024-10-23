#include <freertos/FreeRTOS.h>

#include "PinPatternManager.h"

const char* const TAG = "PinPatternManager";

#include "Chipset.h"
#include "Logging.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

using namespace OpenShock;

PinPatternManager::PinPatternManager(gpio_num_t gpioPin)
  : m_gpioPin(GPIO_NUM_NC)
  , m_pattern()
  , m_taskHandle(nullptr)
  , m_taskMutex()
{
  if (gpioPin == GPIO_NUM_NC) {
    OS_LOGE(TAG, "Pin is not set");
    return;
  }

  if (!IsValidOutputPin(gpioPin)) {
    OS_LOGE(TAG, "Pin %hhi is not a valid output pin", gpioPin);
    return;
  }

  gpio_config_t config;
  config.pin_bit_mask = (1ULL << gpioPin);
  config.mode         = GPIO_MODE_OUTPUT;
  config.pull_up_en   = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type    = GPIO_INTR_DISABLE;
  if (gpio_config(&config) != ESP_OK) {
    OS_LOGE(TAG, "Failed to configure pin %hhi", gpioPin);
    return;
  }

  m_gpioPin = gpioPin;
}

PinPatternManager::~PinPatternManager()
{
  ClearPattern();

  if (m_gpioPin != GPIO_NUM_NC) {
    gpio_reset_pin(m_gpioPin);
  }
}

void PinPatternManager::SetPattern(const State* pattern, std::size_t patternLength)
{
  m_taskMutex.lock(portMAX_DELAY);

  ClearPatternInternal();

  // Set new values
  m_pattern.resize(patternLength);
  std::copy(pattern, pattern + patternLength, m_pattern.begin());

  char name[32];
  snprintf(name, sizeof(name), "PinPatternManager-%hhi", m_gpioPin);

  // Start the task
  BaseType_t result = TaskUtils::TaskCreateUniversal(&Util::FnProxy<&PinPatternManager::RunPattern>, name, 1024, this, 1, &m_taskHandle, 1);  // PROFILED: 0.5KB stack usage
  if (result != pdPASS) {
    OS_LOGE(TAG, "[pin-%hhi] Failed to create task: %d", m_gpioPin, result);

    m_taskHandle = nullptr;
    m_pattern.clear();
  }

  m_taskMutex.unlock();
}

void PinPatternManager::ClearPattern()
{
  m_taskMutex.lock(portMAX_DELAY);

  ClearPatternInternal();

  m_taskMutex.unlock();
}

void PinPatternManager::ClearPatternInternal()
{
  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }

  m_pattern.clear();
}

void PinPatternManager::RunPattern()
{
  while (true) {
    for (const auto& state : m_pattern) {
      gpio_set_level(m_gpioPin, state.level);
      vTaskDelay(pdMS_TO_TICKS(state.duration));
    }
  }
}
