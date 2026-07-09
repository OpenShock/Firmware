#pragma once

#include "Temporal.h"

#include <esp_log.h>

// log_printf is provided by Arduino (esp32-hal-uart) on-device; declared here so
// logging carries no Arduino/hardware include and stays host-portable (the host
// test provides its own definition).
extern "C" int log_printf(const char* fmt, ...);

template<std::size_t N>
constexpr const char* openshockPathToFileName(const char (&path)[N])
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

#define OPENSHOCK_LOG_FORMAT(letter, format) "[%lli][" #letter "][%s:%u] %s(): " format "\r\n", OpenShock::millis(), openshockPathToFileName(__FILE__), __LINE__, __FUNCTION__

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_VERBOSE
#define OS_LOGV(TAG, format, ...) log_printf(OPENSHOCK_LOG_FORMAT(V, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGV(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_DEBUG
#define OS_LOGD(TAG, format, ...) log_printf(OPENSHOCK_LOG_FORMAT(D, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGD(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_INFO
#define OS_LOGI(TAG, format, ...) log_printf(OPENSHOCK_LOG_FORMAT(I, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGI(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_WARN
#define OS_LOGW(TAG, format, ...) log_printf(OPENSHOCK_LOG_FORMAT(W, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGW(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_ERROR
#define OS_LOGE(TAG, format, ...) log_printf(OPENSHOCK_LOG_FORMAT(E, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGE(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

#if OPENSHOCK_LOG_LEVEL >= OPENSHOCK_LOG_LEVEL_NONE
#define OS_LOGN(TAG, format, ...) log_printf(OPENSHOCK_LOG_FORMAT(E, "[%s] " format), TAG, ##__VA_ARGS__)
#else
#define OS_LOGN(TAG, format, ...) \
  do {                            \
  } while (0)
#endif

// OS_PANIC / OS_PANIC_OTA / OS_PANIC_INSTANT live in the separate `panic`
// component (Panic.h) - they pull in esp_ota_ops / esp_system / freertos.
