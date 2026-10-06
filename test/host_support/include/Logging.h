#pragma once

// Host-test stand-in for logging's Logging.h, shared by every host_test suite.
//
// The real header cannot be used on the linux target: its OS_PANIC* macros need
// esp_ota_ops.h (app_update has no linux port), and its output goes through the
// firmware's serial console. Same macro surface here:
//   - OS_LOG* print nothing, but still compile their arguments (inside a dead branch),
//     so a value used only in a log line is still "used" and a log call the firmware
//     could not compile does not compile here either.
//   - OS_PANIC* print the message to stderr and abort(): a panic is never an expected
//     outcome of a test, so it fails the whole run loudly instead of restarting.
//
// sdkconfig.h is included because the real header brings it in (through esp_system.h
// and freertos/FreeRTOS.h), and some sources read CONFIG_* values relying on that.
#include <sdkconfig.h>

#include <cstdio>
#include <cstdlib>

#define OPENSHOCK_LOG_LEVEL_NONE    (0)
#define OPENSHOCK_LOG_LEVEL_ERROR   (1)
#define OPENSHOCK_LOG_LEVEL_WARN    (2)
#define OPENSHOCK_LOG_LEVEL_INFO    (3)
#define OPENSHOCK_LOG_LEVEL_DEBUG   (4)
#define OPENSHOCK_LOG_LEVEL_VERBOSE (5)

// Same shape as the firmware's openshock_log_printf (no format attribute), never called.
inline void openshock_host_log_discard(const char*, const char*, ...)
{
}

#define OPENSHOCK_HOST_LOG(TAG, format, ...)                  \
  do {                                                        \
    if (false) {                                              \
      openshock_host_log_discard(TAG, format, ##__VA_ARGS__); \
    }                                                         \
  } while (0)

#define OS_LOGV(TAG, format, ...) OPENSHOCK_HOST_LOG(TAG, format, ##__VA_ARGS__)
#define OS_LOGD(TAG, format, ...) OPENSHOCK_HOST_LOG(TAG, format, ##__VA_ARGS__)
#define OS_LOGI(TAG, format, ...) OPENSHOCK_HOST_LOG(TAG, format, ##__VA_ARGS__)
#define OS_LOGW(TAG, format, ...) OPENSHOCK_HOST_LOG(TAG, format, ##__VA_ARGS__)
#define OS_LOGE(TAG, format, ...) OPENSHOCK_HOST_LOG(TAG, format, ##__VA_ARGS__)
#define OS_LOGN(TAG, format, ...) OPENSHOCK_HOST_LOG(TAG, format, ##__VA_ARGS__)

#define OS_PANIC_PRINT(TAG, format, ...) std::fprintf(stderr, "[%s] PANIC: " format "\n", TAG, ##__VA_ARGS__)

#define OS_PANIC(TAG, format, ...)              \
  {                                             \
    OS_PANIC_PRINT(TAG, format, ##__VA_ARGS__); \
    std::abort();                               \
  }

#define OS_PANIC_OTA(TAG, format, ...)          \
  {                                             \
    OS_PANIC_PRINT(TAG, format, ##__VA_ARGS__); \
    std::abort();                               \
  }

#define OS_PANIC_INSTANT(TAG, format, ...)      \
  {                                             \
    OS_PANIC_PRINT(TAG, format, ##__VA_ARGS__); \
    std::abort();                               \
  }
