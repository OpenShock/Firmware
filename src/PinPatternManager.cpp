#include "PinPatternManager.h"

#include <Arduino.h>

const char* const TAG = "PinPatternManager";

struct Pattern {
  unsigned int pin;
  ShockLink::PinPatternManager::State* pattern;
  std::size_t patternLength;
}

ShockLink::PinPatternManager::PinPatternManager(unsigned int pin)
  : m_pin(pin)
  , m_pattern(nullptr)
  , m_patternLength(0)
  , m_taskHandle(nullptr)
  , m_taskSemaphore(xSemaphoreCreateBinary())
{
  pinMode(pin, OUTPUT);
}

ShockLink::PinPatternManager::~PinPatternManager() {
  ClearPattern();

  vSemaphoreDelete(m_taskSemaphore);
}

void ShockLink::PinPatternManager::SetPattern(const std::vector<ShockLink::PinPatternManager::PatternState>& pattern) {
  ClearPatternInternal();

  // Set new values
  m_patternLength = pattern.size();
  m_pattern = new State[m_patternLength];
  std::copy(pattern.begin(), pattern.end(), m_pattern);

  char taskName[32];
  snprintf(taskName, sizeof(taskName), "PinPatternManager-%d", m_pin);

  // Start the task
  BaseType_t result = xTaskCreate(RunPattern, taskName, 512, this, 1, &s_taskHandle);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create task: %d", result);
    
    s_taskHandle = nullptr;
    s_ledPattern.clear();
  }

  // Give the semaphore back
  xSemaphoreGive(s_taskSemaphore);
}

void ShockLink::PinPatternManager::ClearPattern() {
  ClearPatternInternal();
  xSemaphoreGive(s_taskSemaphore);
}

void ShockLink::PinPatternManager::ClearPatternInternal() {
  xSemaphoreTake(s_taskSemaphore, portMAX_DELAY);

  if (s_taskHandle != nullptr) {
    vTaskDelete(s_taskHandle);
    s_taskHandle = nullptr;
  }

  if (m_pattern != nullptr) {
    delete[] m_pattern;
    m_pattern = nullptr;
  }
  m_patternLength = 0;
}

void ShockLink::PinPatternManager::RunPattern(void* arg) {
  ShockLink::PinPatternManager* thisPtr = reinterpret_cast<ShockLink::PinPatternManager*>(arg);

  unsigned int pin = thisPtr->m_pin;
  ShockLink::PinPatternManager::State* pattern = thisPtr->m_pattern;
  std::size_t patternLength = thisPtr->m_patternLength;

  while (true) {
    for (std::size_t i = 0; i < patternLength; ++i) {
      digitalWrite(pin, pattern[i].level);
      delay(pattern[i].duration);
    }
  }
}