#pragma once

#include "openshock_board.h"  // OPENSHOCK_LOG_LEVEL, which the OS_LOG* macros below are gated on
#include "sdkconfig.h"
#include "Temporal.h"

#include <esp_ota_ops.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Without it every OS_LOG* (and the OS_PANIC message) in the including file would silently compile to nothing.
#ifndef OPENSHOCK_LOG_LEVEL
#error "OPENSHOCK_LOG_LEVEL is not defined (openshock_board.h)"
#endif

// A level picked in menuconfig wins over the build mode's (the OPENSHOCK_LOG_LEVEL_CHOICE Kconfig choice)
#if defined(CONFIG_OPENSHOCK_LOG_LEVEL_OVERRIDE) && CONFIG_OPENSHOCK_LOG_LEVEL_OVERRIDE >= 0
#undef OPENSHOCK_LOG_LEVEL
#define OPENSHOCK_LOG_LEVEL CONFIG_OPENSHOCK_LOG_LEVEL_OVERRIDE
#endif

// openshock_log_printf forwards to vprintf (the IDF console: UART0 + any secondary
// USB-Serial-JTAG/CDC output). Defined in Logging.cpp so the logging path carries
// no hardware include of its own. The OS_PANIC* macros below, however, pull in
// esp_ota_ops / esp_system / freertos, so this header is firmware-only (host tests
// shadow it with a no-op stub).
// Named distinctly from the framework's own log_printf to avoid a symbol clash.
extern "C" int openshock_log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

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

#define OPENSHOCK_LOG_FORMAT(letter, format) "[%lli][" #letter "][%s:%d] %s(): " format "\r\n", OpenShock::millis(), openshockPathToFileName(__FILE__), __LINE__, __FUNCTION__

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_VERBOSE
#define OS_LOGV(TAG, format, ...) openshock_log_printf(OPENSHOCK_LOG_FORMAT(V, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGV(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_DEBUG
#define OS_LOGD(TAG, format, ...) openshock_log_printf(OPENSHOCK_LOG_FORMAT(D, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGD(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_INFO
#define OS_LOGI(TAG, format, ...) openshock_log_printf(OPENSHOCK_LOG_FORMAT(I, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGI(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_WARN
#define OS_LOGW(TAG, format, ...) openshock_log_printf(OPENSHOCK_LOG_FORMAT(W, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGW(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_ERROR
#define OS_LOGE(TAG, format, ...) openshock_log_printf(OPENSHOCK_LOG_FORMAT(E, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGE(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

// Always compiled in: NONE is the lowest level, so no OPENSHOCK_LOG_LEVEL disables it.
#define OS_LOGN(TAG, format, ...) openshock_log_printf(OPENSHOCK_LOG_FORMAT(N, "[%s] " format), TAG, ##__VA_ARGS__)

// Panic/abort macros. These log at error level, then restart the device; they
// pull in esp_ota_ops / esp_system / freertos (included above), so anything that
// includes Logging.h is firmware-only.
#define OS_PANIC_PRINT(TAG, format, ...) OS_LOGE(TAG, "PANIC: " format, ##__VA_ARGS__)

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
