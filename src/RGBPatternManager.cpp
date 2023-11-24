#include "RGBPatternManager.h"

#include "Logging.h"
#include "util/TaskUtils.h"

#include <Arduino.h>

const char* const TAG = "RGBPatternManager";

using namespace OpenShock;

// Currently this assumes a single WS2812B LED
// TODO: Support multiple LEDs ?
// TODO: Support other LED types ?

RGBPatternManager::RGBPatternManager(std::uint8_t rgbPin) : m_rgbPin(rgbPin), m_rgbBrightness(255), m_pattern(nullptr), m_patternLength(0), m_taskHandle(nullptr), m_taskSemaphore(xSemaphoreCreateBinary()) {
  if ((m_rmtHandle = rmtInit(m_rgbPin, RMT_TX_MODE, RMT_MEM_64)) == NULL) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create rgb rmt object", m_rgbPin);
  }

  float realTick = rmtSetTick(m_rmtHandle, 100);
  ESP_LOGD(TAG, "[pin-%u] real tick set to: %fns", m_rgbPin, realTick);

  SetBrightness(20);

  xSemaphoreGive(m_taskSemaphore);
}

RGBPatternManager::~RGBPatternManager() {
  ClearPattern();

  vSemaphoreDelete(m_taskSemaphore);
}

void RGBPatternManager::SetPattern(nonstd::span<const RGBState> pattern) {
  ClearPatternInternal();

  // Set new values
  m_patternLength = pattern.size();
  m_pattern       = new RGBState[m_patternLength];
  std::copy(pattern.begin(), pattern.end(), m_pattern);

  // Start the task
  BaseType_t result = TaskUtils::TaskCreateExpensive(RunPattern, TAG, 4096, this, 1, &m_taskHandle);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create task: %d", m_rgbPin, result);

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

void RGBPatternManager::ClearPattern() {
  ClearPatternInternal();
  xSemaphoreGive(m_taskSemaphore);
}

void RGBPatternManager::ClearPatternInternal() {
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

// Range: 0-255
void RGBPatternManager::SetBrightness(std::uint8_t brightness) {
  m_rgbBrightness = brightness;
}

void RGBPatternManager::SendRGB(const RGBState state) {
  const std::uint16_t bitCount = (8 * 3) * (1);  // 8 bits per color * 3 colors * 1 LED

  rmt_data_t led_data[24];

  std::uint8_t r = (std::uint8_t)((std::uint16_t)state.red * m_rgbBrightness / 255);
  std::uint8_t g = (std::uint8_t)((std::uint16_t)state.green * m_rgbBrightness / 255);
  std::uint8_t b = (std::uint8_t)((std::uint16_t)state.blue * m_rgbBrightness / 255);

  std::uint8_t led, col, bit;
  std::uint8_t i = 0;
  // WS2812B takes commands in GRB order
  // https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf - Page 5
  std::uint8_t colors[3] = {g, r, b};
  for (led = 0; led < 1; led++) {
    for (col = 0; col < 3; col++) {
      for (bit = 0; bit < 8; bit++) {
        if ((colors[col] & (1 << (7 - bit)))) {
          led_data[i].level0    = 1;
          led_data[i].duration0 = 8;
          led_data[i].level1    = 0;
          led_data[i].duration1 = 4;
        } else {
          led_data[i].level0    = 1;
          led_data[i].duration0 = 4;
          led_data[i].level1    = 0;
          led_data[i].duration1 = 8;
        }
        i++;
      }
    }
  }
  // Send the data
  rmtWriteBlocking(m_rmtHandle, led_data, bitCount);
}

void RGBPatternManager::RunPattern(void* arg) {
  RGBPatternManager* thisPtr = reinterpret_cast<RGBPatternManager*>(arg);

  RGBPatternManager::RGBState* pattern = thisPtr->m_pattern;
  std::size_t patternLength            = thisPtr->m_patternLength;

  while (true) {
    for (std::size_t i = 0; i < patternLength; ++i) {
      thisPtr->SendRGB(pattern[i]);
      vTaskDelay(pdMS_TO_TICKS(pattern[i].duration));
    }
  }
}
