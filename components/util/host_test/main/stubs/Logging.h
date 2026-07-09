#pragma once

// Host-test stub for the firmware Logging.h. The real one pulls in esp_ota_ops
// (app_update) and Arduino's log_printf, neither of which exists on the linux
// host target. util's pure sources only use these macros for diagnostics, so
// no-op them - the logic under test does not depend on logging.
#define OS_LOGV(tag, ...) ((void)0)
#define OS_LOGD(tag, ...) ((void)0)
#define OS_LOGI(tag, ...) ((void)0)
#define OS_LOGW(tag, ...) ((void)0)
#define OS_LOGE(tag, ...) ((void)0)
#define OS_LOGWTF(tag, ...) ((void)0)
#define OS_PANIC(tag, ...) ((void)0)
#define OS_PANIC_OTA(tag, ...) ((void)0)
