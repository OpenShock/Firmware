#pragma once
// Host-test stub: the real Panic.h pulls esp_ota_ops/esp_system/freertos.
#include <cstdlib>
#define OS_PANIC(tag, ...)         ::abort()
#define OS_PANIC_OTA(tag, ...)     ::abort()
#define OS_PANIC_INSTANT(tag, ...) ::abort()
