#pragma once

// Host-test stub for the firmware Logging.h. The real one writes to the serial
// transport, which doesn't exist on the linux host target. The encoder sources
// only use these macros for diagnostics, so no-op them.
#define OS_LOGV(tag, ...)      ((void)0)
#define OS_LOGD(tag, ...)      ((void)0)
#define OS_LOGI(tag, ...)      ((void)0)
#define OS_LOGW(tag, ...)      ((void)0)
#define OS_LOGE(tag, ...)      ((void)0)
#define OS_LOGN(tag, ...)      ((void)0)
#define OS_LOGWTF(tag, ...)    ((void)0)
#define OS_PANIC(tag, ...)     ((void)0)
#define OS_PANIC_OTA(tag, ...) ((void)0)
