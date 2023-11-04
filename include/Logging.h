#pragma once

#include <esp32-hal-log.h>
#include <esp_log.h>

#define ESP_PANIC(TAG, format, ...)                                              \
  ESP_LOGE(TAG, "PANIC: " format ", restarting in 5 seconds...", ##__VA_ARGS__); \
  vTaskDelay(pdMS_TO_TICKS(5000));                                               \
  esp_restart();

#define ESP_PANIC_INSTANT(TAG, format, ...)                             \
  ESP_LOGE(TAG, "PANIC: " format ", restarting now...", ##__VA_ARGS__); \
  esp_restart();
