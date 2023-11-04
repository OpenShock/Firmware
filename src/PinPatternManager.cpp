#include "PinPatternManager.h"

#include "Logging.h"

#include <Arduino.h>

const char* const TAG = "PinPatternManager";

struct Pattern {
  unsigned int pin;
  OpenShock::PinPatternManager::State* pattern;
  std::size_t patternLength;
};

using namespace OpenShock;

PinPatternManager::PinPatternManager(unsigned int gpioPin) : m_gpioPin(gpioPin), m_pattern(nullptr), m_patternLength(0), m_taskHandle(nullptr), m_taskSemaphore(xSemaphoreCreateBinary()) {
  pinMode(gpioPin, OUTPUT);
  xSemaphoreGive(m_taskSemaphore);
}

PinPatternManager::~PinPatternManager() {
  ClearPattern();

  vSemaphoreDelete(m_taskSemaphore);
}

void PinPatternManager::SetPattern(nonstd::span<const State> pattern) {
  ClearPatternInternal();

  // Set new values
  m_patternLength = pattern.size();
  m_pattern       = new State[m_patternLength];
  std::copy(pattern.begin(), pattern.end(), m_pattern);

  char name[32];
  snprintf(name, sizeof(name), "PinPatternManager-%u", m_gpioPin);

  // Start the task
  BaseType_t result = xTaskCreate(RunPattern, name, 1024, this, 1, &m_taskHandle);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create task: %d", m_gpioPin, result);

    m_taskHandle = nullptr;

    if (m_pattern != nullptr) {
      delete[] m_pattern;
      m_pattern = nullptr;
    }
    m_patternLength = 0;
  }

  // Give the semaphore back
  xSemaphoreGive(m_taskSemaphore);
}

void PinPatternManager::ClearPattern() {
  ClearPatternInternal();
  xSemaphoreGive(m_taskSemaphore);
}

void PinPatternManager::ClearPatternInternal() {
  xSemaphoreTake(m_taskSemaphore, portMAX_DELAY);

  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }

  if (m_pattern != nullptr) {
    delete[] m_pattern;
    m_pattern = nullptr;
  }
  m_patternLength = 0;
}

void PinPatternManager::RunPattern(void* arg) {
  PinPatternManager* thisPtr = reinterpret_cast<PinPatternManager*>(arg);

  unsigned int pin                  = thisPtr->m_gpioPin;
  PinPatternManager::State* pattern = thisPtr->m_pattern;
  std::size_t patternLength         = thisPtr->m_patternLength;

  while (true) {
    for (std::size_t i = 0; i < patternLength; ++i) {
      digitalWrite(pin, pattern[i].level);
      vTaskDelay(pdMS_TO_TICKS(pattern[i].duration));
    }
  }
}
