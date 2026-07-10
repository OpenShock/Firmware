#pragma once

// Host-test stub for the firmware Logging.h. The real one pulls in IDF logging
// components (esp_log and friends) that don't exist on the linux host target.
// util's pure sources only use these macros for diagnostics, so no-op them -
// the logic under test does not depend on logging.
#define OS_LOGV(tag, ...)      ((void)0)
#define OS_LOGD(tag, ...)      ((void)0)
#define OS_LOGI(tag, ...)      ((void)0)
#define OS_LOGW(tag, ...)      ((void)0)
#define OS_LOGE(tag, ...)      ((void)0)
#define OS_LOGWTF(tag, ...)    ((void)0)
// OS_PANIC* are intentionally NOT defined here — they live in Panic.h (stubbed
// separately). Defining them here too would double-define when a source pulls both.
