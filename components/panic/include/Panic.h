#pragma once

// Panic/abort macros split out of Logging.h so that `logging` stays portable
// (no esp_ota_ops / esp_system / freertos). These restart the device and are
// firmware-only.
#include "Logging.h"  // OS_LOGE (via OS_PANIC_PRINT)

#include <esp_ota_ops.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define OS_PANIC_PRINT(TAG, format, ...) OS_LOGE(TAG, "PANIC: " format, ##__VA_ARGS__)

#define OS_PANIC(TAG, format, ...)                                             \
  {                                                                            \
    OS_PANIC_PRINT(TAG, format ", restarting in 5 seconds...", ##__VA_ARGS__); \
    vTaskDelay(pdMS_TO_TICKS(5000));                                           \
    esp_restart();                                                             \
  }

#define OS_PANIC_OTA(TAG, format, ...)                                                                           \
  {                                                                                                              \
    OS_PANIC_PRINT(TAG, format ", invalidating update partition and restarting in 5 seconds...", ##__VA_ARGS__); \
    vTaskDelay(pdMS_TO_TICKS(5000));                                                                             \
    esp_ota_mark_app_invalid_rollback_and_reboot();                                                              \
    esp_restart();                                                                                               \
  }

#define OS_PANIC_INSTANT(TAG, format, ...)      \
  {                                             \
    OS_PANIC_PRINT(TAG, format, ##__VA_ARGS__); \
    esp_restart();                              \
  }
