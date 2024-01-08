#include "PinPatternManager.h"

#include "Chipset.h"
#include "Logging.h"

const char* const TAG = "PinPatternManager";

using namespace OpenShock;

PinPatternManager::PinPatternManager(gpio_num_t gpioPin) : m_gpioPin(GPIO_NUM_NC), m_pattern(), m_taskHandle(nullptr), m_taskMutex(xSemaphoreCreateMutex()) {
  if (!IsValidOutputPin(gpioPin)) {
    ESP_LOGE(TAG, "Pin %d is not a valid output pin", gpioPin);
    return;
  }

  gpio_config_t config;
  config.pin_bit_mask = (1ULL << gpioPin);
  config.mode         = GPIO_MODE_OUTPUT;
  config.pull_up_en   = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type    = GPIO_INTR_DISABLE;
  if (gpio_config(&config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure pin %d", gpioPin);
    return;
  }

  m_gpioPin = gpioPin;
}

PinPatternManager::~PinPatternManager() {
  ClearPattern();

  vSemaphoreDelete(m_taskMutex);

  if (m_gpioPin != GPIO_NUM_NC) {
    gpio_reset_pin(m_gpioPin);
  }
}

void PinPatternManager::SetPattern(const State* pattern, std::size_t patternLength) {
  ClearPatternInternal();

  // Set new values
  m_pattern.resize(patternLength);
  std::copy(pattern, pattern + patternLength, m_pattern.begin());

  char name[32];
  snprintf(name, sizeof(name), "PinPatternManager-%d", m_gpioPin);

  // Start the task
  BaseType_t result = xTaskCreate(RunPattern, name, 1024, this, 1, &m_taskHandle);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create task: %d", m_gpioPin, result);

    m_taskHandle = nullptr;
    m_pattern.clear();
  }

  // Give the semaphore back
  xSemaphoreGive(m_taskMutex);
}

void PinPatternManager::ClearPattern() {
  ClearPatternInternal();
  xSemaphoreGive(m_taskMutex);
}

void PinPatternManager::ClearPatternInternal() {
  xSemaphoreTake(m_taskMutex, portMAX_DELAY);

  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }

  m_pattern.clear();
}

void PinPatternManager::RunPattern(void* arg) {
  PinPatternManager* thisPtr = reinterpret_cast<PinPatternManager*>(arg);

  gpio_num_t pin              = thisPtr->m_gpioPin;
  std::vector<State>& pattern = thisPtr->m_pattern;

  while (true) {
    for (const auto& state : pattern) {
      gpio_set_level(pin, state.level);
      vTaskDelay(pdMS_TO_TICKS(state.duration));
    }
  }
}
