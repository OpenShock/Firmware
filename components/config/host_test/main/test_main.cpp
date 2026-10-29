// Host-only (linux target) test binary for the config component: the typed config
// sections, their flatbuffer/JSON (de)serialization, and the Config:: store on top
// of an in-memory ConfigFs. Runs on ESP-IDF's FreeRTOS POSIX port, which starts the
// scheduler and calls app_main from its main task, so the store's SimpleMutexes are
// the real one. The exit code is the Unity failure count.
#include "unity.h"

#include "BoardDescriptor.h"

#include <cstdlib>

extern "C" void app_main(void)
{
  OpenShock::Board::Init();  // The test board (test/host_support), as app_main does on the device

  UNITY_BEGIN();
  unity_run_all_tests();
  std::exit(UNITY_END());
}
