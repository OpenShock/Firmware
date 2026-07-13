#pragma once

// Host-test stub for the firmware Logging.h. The real one pulls in esp_system /
// esp_ota_ops / freertos (for the OS_PANIC* macros) and the `serial` transport,
// none of which exist on the linux host target. common's pure sources only use
// these macros for diagnostics, so no-op the logging ones and abort on panic -
// the logic under test does not depend on logging.
#include <cstdlib>
#define OS_LOGV(tag, ...)      ((void)0)
#define OS_LOGD(tag, ...)      ((void)0)
#define OS_LOGI(tag, ...)      ((void)0)
#define OS_LOGW(tag, ...)      ((void)0)
#define OS_LOGE(tag, ...)      ((void)0)
#define OS_LOGN(tag, ...)      ((void)0)
#define OS_LOGWTF(tag, ...)    ((void)0)
#define OS_PANIC(tag, ...)         ::abort()
#define OS_PANIC_OTA(tag, ...)     ::abort()
#define OS_PANIC_INSTANT(tag, ...) ::abort()
