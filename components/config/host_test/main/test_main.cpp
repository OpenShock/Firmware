// Host-only (linux target) test binary for the config component: the typed config
// sections, their flatbuffer/JSON (de)serialization, and the Config:: store on top
// of an in-memory ConfigFs. Runs on ESP-IDF's FreeRTOS POSIX port, which starts the
// scheduler and calls app_main from its main task, so the store's ReadWriteMutex is
// the real one. The exit code is the Unity failure count.
#include "unity.h"

#include <cstdlib>

extern "C" void app_main(void)
{
  UNITY_BEGIN();
  unity_run_all_tests();
  std::exit(UNITY_END());
}
