#include "RGBPatternManager.h"

#include "Chipset.h"
#include "Logging.h"
#include "util/TaskUtils.h"

#include <array>

const char* const TAG = "RGBPatternManager";

using namespace OpenShock;

// Currently this assumes a single WS2812B LED
// TODO: Support multiple LEDs ?
// TODO: Support other LED types ?

RGBPatternManager::RGBPatternManager(std::uint8_t rgbPin) : m_rgbPin(255), m_brightness(255), m_pattern(), m_rmtHandle(nullptr), m_taskHandle(nullptr), m_taskMutex(xSemaphoreCreateMutex()) {
  if (!OpenShock::IsValidOutputPin(rgbPin)) {
    ESP_LOGE(TAG, "Pin %d is not a valid output pin", rgbPin);
    return;
  }

  m_rmtHandle = rmtInit(rgbPin, RMT_TX_MODE, RMT_MEM_64);
  if (m_rmtHandle == NULL) {
    ESP_LOGE(TAG, "Failed to initialize RMT for pin %d", rgbPin);
    return;
  }

  float realTick = rmtSetTick(m_rmtHandle, 100.F);
  ESP_LOGD(TAG, "RMT tick is %f ns for pin %d", realTick, rgbPin);

  SetBrightness(20);

  m_rgbPin = rgbPin;
}

RGBPatternManager::~RGBPatternManager() {
  ClearPattern();

  vSemaphoreDelete(m_taskMutex);
}

void RGBPatternManager::SetPattern(const RGBState* pattern, std::size_t patternLength) {
  ClearPatternInternal();

  // Set new values
  m_pattern.resize(patternLength);
  std::copy(pattern, pattern + patternLength, m_pattern.begin());

  // Start the task
  BaseType_t result = TaskUtils::TaskCreateExpensive(RunPattern, TAG, 4096, this, 1, &m_taskHandle);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "[pin-%u] Failed to create task: %d", m_rgbPin, result);

    m_taskHandle = nullptr;
    m_pattern.clear();
  }

  // Give the semaphore back
  xSemaphoreGive(m_taskMutex);
}

void RGBPatternManager::ClearPattern() {
  ClearPatternInternal();
  xSemaphoreGive(m_taskMutex);
}

void RGBPatternManager::ClearPatternInternal() {
  xSemaphoreTake(m_taskMutex, portMAX_DELAY);

  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }

  m_pattern.clear();
}

// Range: 0-255
void RGBPatternManager::SetBrightness(std::uint8_t brightness) {
  m_brightness = brightness;
}

void RGBPatternManager::RunPattern(void* arg) {
  RGBPatternManager* thisPtr = reinterpret_cast<RGBPatternManager*>(arg);

  rmt_obj_t* rmtHandle           = thisPtr->m_rmtHandle;
  std::uint8_t brightness        = thisPtr->m_brightness;
  std::vector<RGBState>& pattern = thisPtr->m_pattern;

  std::array<rmt_data_t, 24> led_data;  // 24 bits per LED (8 bits per color * 3 colors)

  while (true) {
    for (const auto& state : pattern) {
      std::uint8_t r = static_cast<std::uint8_t>(static_cast<std::uint16_t>(state.red) * brightness / 255);
      std::uint8_t g = static_cast<std::uint8_t>(static_cast<std::uint16_t>(state.green) * brightness / 255);
      std::uint8_t b = static_cast<std::uint8_t>(static_cast<std::uint16_t>(state.blue) * brightness / 255);

      // WS2812B takes commands in GRB order
      // https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf - Page 5
      const std::uint32_t colors = (static_cast<std::uint32_t>(g) << 16) | (static_cast<std::uint32_t>(r) << 8) | static_cast<std::uint32_t>(b);

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

      // Send the data
      rmtWriteBlocking(rmtHandle, led_data.data(), led_data.size());
      vTaskDelay(pdMS_TO_TICKS(state.duration));
    }
  }
}
