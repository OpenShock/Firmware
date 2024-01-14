#pragma once

#include <esp32-hal-log.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#define ESP_PANIC_PRINT(TAG, format, ...) ESP_LOGE(TAG, "PANIC: " format, ##__VA_ARGS__)

#define ESP_PANIC(TAG, format, ...)                                           \
  ESP_PANIC_PRINT(TAG, format ", restarting in 5 seconds...", ##__VA_ARGS__); \
  vTaskDelay(pdMS_TO_TICKS(5000));                                            \
  esp_restart()

#define ESP_PANIC_OTA(TAG, format, ...)                                                                         \
  ESP_PANIC_PRINT(TAG, format ", invalidating update partition and restarting in 5 seconds...", ##__VA_ARGS__); \
  vTaskDelay(pdMS_TO_TICKS(5000));                                                                              \
  esp_ota_mark_app_invalid_rollback_and_reboot();                                                               \
  esp_restart()

#define ESP_PANIC_INSTANT(TAG, format, ...)    \
  ESP_PANIC_PRINT(TAG, format, ##__VA_ARGS__); \
  esp_restart()
