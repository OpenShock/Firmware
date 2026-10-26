#pragma once

#include "openshock_board.h"  // OPENSHOCK_LOG_LEVEL, which the OS_LOG* macros below are gated on
#include "Temporal.h"

#include <esp_ota_ops.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Without it every OS_LOG* (and the OS_PANIC message) in the including file would silently compile to nothing.
#ifndef OPENSHOCK_LOG_LEVEL
#error "OPENSHOCK_LOG_LEVEL is not defined (openshock_board.h)"
#endif

// openshock_log_write formats the message, writes "[uptime][L][file:line] func(): [tag] message" to the serial console
// and hands level, tag and message to the log tap (LogTap.h). `level` is the severity letter: V, D, I, W, E, N, or C
// for a panic. Defined in Logging.cpp so the logging path carries no hardware include of its own. The OS_PANIC* macros
// below, however, pull in esp_ota_ops / esp_system / freertos, so this header is firmware-only (host tests shadow it
// with a no-op stub).
extern "C" void openshock_log_write(char level, const char* tag, const char* file, int line, const char* func, const char* fmt, ...) __attribute__((format(printf, 6, 7)));

// Only ever called with __FILE__; consteval guarantees the directory is stripped at compile time.
template<std::size_t N>
consteval const char* openshockPathToFileName(const char (&path)[N])
{
  std::size_t pos = 0;
  for (std::size_t i = 0; i < N; i++) {
    char c = path[i];
    if (c == '/' || c == '\\') {
      pos = i + 1;
    }
  }
  return path + pos;
}

#define OPENSHOCK_LOG_LEVEL_NONE    (0)
#define OPENSHOCK_LOG_LEVEL_ERROR   (1)
#define OPENSHOCK_LOG_LEVEL_WARN    (2)
#define OPENSHOCK_LOG_LEVEL_INFO    (3)
#define OPENSHOCK_LOG_LEVEL_DEBUG   (4)
#define OPENSHOCK_LOG_LEVEL_VERBOSE (5)

#define OPENSHOCK_LOG_WRITE(letter, TAG, format, ...) openshock_log_write(letter, TAG, openshockPathToFileName(__FILE__), __LINE__, __FUNCTION__, format, ##__VA_ARGS__)

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_VERBOSE
#define OS_LOGV(TAG, format, ...) OPENSHOCK_LOG_WRITE('V', TAG, format, ##__VA_ARGS__)
#else
#define OS_LOGV(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_DEBUG
#define OS_LOGD(TAG, format, ...) OPENSHOCK_LOG_WRITE('D', TAG, format, ##__VA_ARGS__)
#else
#define OS_LOGD(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_INFO
#define OS_LOGI(TAG, format, ...) OPENSHOCK_LOG_WRITE('I', TAG, format, ##__VA_ARGS__)
#else
#define OS_LOGI(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_WARN
#define OS_LOGW(TAG, format, ...) OPENSHOCK_LOG_WRITE('W', TAG, format, ##__VA_ARGS__)
#else
#define OS_LOGW(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_ERROR
#define OS_LOGE(TAG, format, ...) OPENSHOCK_LOG_WRITE('E', TAG, format, ##__VA_ARGS__)
#else
#define OS_LOGE(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

// Always compiled in: NONE is the lowest level, so no OPENSHOCK_LOG_LEVEL disables it.
#define OS_LOGN(TAG, format, ...) OPENSHOCK_LOG_WRITE('N', TAG, format, ##__VA_ARGS__)

// Panic/abort macros. These log at critical level (C), then restart the device; they
// pull in esp_ota_ops / esp_system / freertos (included above), so anything that
// includes Logging.h is firmware-only.
// Printed with the letter C (critical), so diagnostics and anyone reading the console can tell a panic from an error.
#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_ERROR
#define OS_PANIC_PRINT(TAG, format, ...) OPENSHOCK_LOG_WRITE('C', TAG, "PANIC: " format, ##__VA_ARGS__)
#else
#define OS_PANIC_PRINT(TAG, format, ...) \
  do {                                   \
  } while (0)
#endif

#define OS_PANIC(TAG, format, ...)                                             \
  do {                                                                         \
    OS_PANIC_PRINT(TAG, format ", restarting in 5 seconds...", ##__VA_ARGS__); \
    vTaskDelay(pdMS_TO_TICKS(5000));                                           \
    esp_restart();                                                             \
  } while (0)

#define OS_PANIC_OTA(TAG, format, ...)                                                                           \
  do {                                                                                                           \
    OS_PANIC_PRINT(TAG, format ", invalidating update partition and restarting in 5 seconds...", ##__VA_ARGS__); \
    vTaskDelay(pdMS_TO_TICKS(5000));                                                                             \
    esp_ota_mark_app_invalid_rollback_and_reboot();                                                              \
    esp_restart();                                                                                               \
  } while (0)

#define OS_PANIC_INSTANT(TAG, format, ...)      \
  do {                                          \
    OS_PANIC_PRINT(TAG, format, ##__VA_ARGS__); \
    esp_restart();                              \
  } while (0)
